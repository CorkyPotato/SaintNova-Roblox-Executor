#include "saintnova/harness_api.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "lua.h"
#include "lualib.h"

#include "saintnova/luau_vm.hpp"

namespace saintnova {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxFileBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaxListEntries = 10000;
constexpr std::size_t kMaxClipboardBytes = 4u * 1024u * 1024u;

constexpr const char* kWorkspaceKey = "saintnova.workspace";
constexpr const char* kFilesystemKey = "saintnova.cap.filesystem";
constexpr const char* kClipboardKey = "saintnova.cap.clipboard";
constexpr const char* kNewClosuresKey = "saintnova.newcclosures";
constexpr const char* kBytecodeKey = "saintnova.bytecode";

[[noreturn]] void fail(lua_State* L, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    luaL_error(L, "%s", buffer);
    std::abort();
}

std::string utf8_of(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

fs::path path_from_utf8(const std::string& text) {
    const std::u8string value(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return fs::path(value);
}

bool registry_flag(lua_State* L, const char* key) {
    lua_getfield(L, LUA_REGISTRYINDEX, key);
    const bool value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

std::string registry_string(lua_State* L, const char* key) {
    std::string value;
    lua_getfield(L, LUA_REGISTRYINDEX, key);
    std::size_t length = 0;
    const char* text = lua_tolstring(L, -1, &length);
    if (text != nullptr) value.assign(text, length);
    lua_pop(L, 1);
    return value;
}

void require_capability(lua_State* L, const char* key, const char* capability) {
    if (!registry_flag(L, key)) fail(L, "%s is disabled by the host configuration", capability);
}

// Resolves a script-supplied relative path inside the workspace root.
bool resolve_path(lua_State* L, int index, fs::path& out, std::string& error) {
    std::size_t length = 0;
    const char* raw = luaL_checklstring(L, index, &length);
    const std::string text(raw, length);
    if (text.empty()) {
        error = "path is empty";
        return false;
    }
    if (text.size() > 512) {
        error = "path is too long";
        return false;
    }

    fs::path relative = path_from_utf8(text);
    if (relative.has_root_name() || relative.has_root_directory() || relative.is_absolute()) {
        error = "path must be relative to the workspace";
        return false;
    }
    for (const fs::path& part : relative) {
        if (part == "..") {
            error = "path may not leave the workspace";
            return false;
        }
    }

    const fs::path root = path_from_utf8(registry_string(L, kWorkspaceKey));
    if (root.empty()) {
        error = "workspace root is not configured";
        return false;
    }
    out = root / relative;
    return true;
}

bool read_file(const fs::path& path, std::string& contents, std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        error = "no such file";
        return false;
    }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        error = "cannot stat file: " + ec.message();
        return false;
    }
    if (size > kMaxFileBytes) {
        error = "file exceeds the 8 MiB harness limit";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open file for reading";
        return false;
    }
    contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = "read failed";
        return false;
    }
    return true;
}

bool write_file(const fs::path& path, const char* data, std::size_t length, bool append, std::string& error) {
    if (length > kMaxFileBytes) {
        error = "content exceeds the 8 MiB harness limit";
        return false;
    }
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        error = "path is a folder";
        return false;
    }
    const fs::path parent = path.parent_path();
    if (!parent.empty() && !fs::is_directory(parent, ec)) {
        error = "parent folder does not exist";
        return false;
    }
    std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) {
        error = "cannot open file for writing";
        return false;
    }
    out.write(data, static_cast<std::streamsize>(length));
    if (!out) {
        error = "write failed";
        return false;
    }
    return true;
}

// -- filesystem bindings ----------------------------------------------------

int l_readfile(lua_State* L) {
    require_capability(L, kFilesystemKey, "readfile");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "readfile: %s", error.c_str());
    std::string contents;
    if (!read_file(path, contents, error)) luaL_error(L, "readfile: %s", error.c_str());
    lua_pushlstring(L, contents.data(), contents.size());
    return 1;
}

int l_writefile(lua_State* L) {
    require_capability(L, kFilesystemKey, "writefile");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "writefile: %s", error.c_str());
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 2, &length);
    if (!write_file(path, data, length, false, error)) luaL_error(L, "writefile: %s", error.c_str());
    return 0;
}

int l_appendfile(lua_State* L) {
    require_capability(L, kFilesystemKey, "appendfile");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "appendfile: %s", error.c_str());
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 2, &length);
    if (!write_file(path, data, length, true, error)) luaL_error(L, "appendfile: %s", error.c_str());
    return 0;
}

int l_isfile(lua_State* L) {
    require_capability(L, kFilesystemKey, "isfile");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "isfile: %s", error.c_str());
    std::error_code ec;
    lua_pushboolean(L, fs::is_regular_file(path, ec));
    return 1;
}

int l_isfolder(lua_State* L) {
    require_capability(L, kFilesystemKey, "isfolder");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "isfolder: %s", error.c_str());
    std::error_code ec;
    lua_pushboolean(L, fs::is_directory(path, ec));
    return 1;
}

int l_makefolder(lua_State* L) {
    require_capability(L, kFilesystemKey, "makefolder");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "makefolder: %s", error.c_str());
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec && !fs::is_directory(path, ec)) luaL_error(L, "makefolder: %s", ec.message().c_str());
    return 0;
}

int l_delfile(lua_State* L) {
    require_capability(L, kFilesystemKey, "delfile");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "delfile: %s", error.c_str());
    std::error_code ec;
    if (fs::is_directory(path, ec)) luaL_error(L, "delfile: path is a folder");
    const bool removed = fs::remove(path, ec);
    if (ec) luaL_error(L, "delfile: %s", ec.message().c_str());
    if (!removed) luaL_error(L, "delfile: no such file");
    lua_pushboolean(L, 1);
    return 1;
}

int l_delfolder(lua_State* L) {
    require_capability(L, kFilesystemKey, "delfolder");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "delfolder: %s", error.c_str());
    std::error_code ec;
    if (!fs::is_directory(path, ec)) luaL_error(L, "delfolder: no such folder");
    const std::uintmax_t removed = fs::remove_all(path, ec);
    if (ec) luaL_error(L, "delfolder: %s", ec.message().c_str());
    lua_pushboolean(L, removed > 0);
    return 1;
}

int l_listfiles(lua_State* L) {
    require_capability(L, kFilesystemKey, "listfiles");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "listfiles: %s", error.c_str());

    std::error_code ec;
    if (!fs::is_directory(path, ec)) luaL_error(L, "listfiles: no such folder");

    std::vector<std::string> names;
    fs::directory_iterator it(path, ec);
    if (ec) luaL_error(L, "listfiles: %s", ec.message().c_str());
    for (const fs::directory_entry& entry : it) {
        if (names.size() >= kMaxListEntries) break;
        names.push_back(utf8_of(entry.path().filename()));
    }
    std::sort(names.begin(), names.end());

    lua_createtable(L, static_cast<int>(names.size()), 0);
    int index = 1;
    for (const std::string& name : names) {
        lua_pushlstring(L, name.data(), name.size());
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int l_loadfile(lua_State* L) {
    require_capability(L, kFilesystemKey, "loadfile");
    fs::path path;
    std::string error;
    if (!resolve_path(L, 1, path, error)) luaL_error(L, "loadfile: %s", error.c_str());
    std::string contents;
    if (!read_file(path, contents, error)) luaL_error(L, "loadfile: %s", error.c_str());

    std::string bytecode;
    std::string compile_error;
    if (!LuauVm::compile_to_bytecode(contents, bytecode, compile_error)) {
        luaL_error(L, "loadfile: %s", compile_error.c_str());
    }
    std::size_t length = 0;
    const char* raw = luaL_checklstring(L, 1, &length);
    std::string chunk = "=" + std::string(raw, length);
    if (luau_load(L, chunk.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
        const char* message = lua_tostring(L, -1);
        luaL_error(L, "loadfile: %s", message != nullptr ? message : "failed to load chunk");
    }
    return 1;
}

// -- clipboard --------------------------------------------------------------

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0) return std::wstring();
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                         nullptr);
    if (size <= 0) return std::string();
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

bool open_clipboard(std::string& error) {
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (OpenClipboard(nullptr) != FALSE) return true;
        Sleep(15);
    }
    error = "clipboard is held by another process";
    return false;
}

bool clipboard_set(const std::string& text, std::string& error) {
    if (text.size() > kMaxClipboardBytes) {
        error = "text exceeds the 4 MiB clipboard limit";
        return false;
    }
    const std::wstring wide = widen(text);
    if (wide.size() != text.size() && text.find('\0') == std::string::npos) {
        // widen() legitimately produces fewer units for multi-byte UTF-8;
        // only reject when conversion failed outright.
        if (wide.empty() && !text.empty()) {
            error = "text is not valid UTF-8";
            return false;
        }
    }
    if (!open_clipboard(error)) return false;

    bool ok = false;
    if (EmptyClipboard() != FALSE) {
        const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (handle != nullptr) {
            void* target = GlobalLock(handle);
            if (target != nullptr) {
                std::memcpy(target, wide.c_str(), bytes);
                GlobalUnlock(handle);
                if (SetClipboardData(CF_UNICODETEXT, handle) != nullptr) {
                    ok = true;
                    handle = nullptr;  // ownership transferred to the clipboard
                }
            }
            if (handle != nullptr) GlobalFree(handle);
        }
    }
    CloseClipboard();
    if (!ok) error = "the clipboard rejected the update";
    return ok;
}

bool clipboard_get(std::string& text, std::string& error) {
    if (!open_clipboard(error)) return false;
    bool ok = false;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE) {
        HANDLE handle = GetClipboardData(CF_UNICODETEXT);
        if (handle != nullptr) {
            const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(handle));
            if (data != nullptr) {
                text = narrow(std::wstring(data));
                GlobalUnlock(handle);
                ok = true;
            }
        }
    } else if (IsClipboardFormatAvailable(CF_TEXT) != FALSE) {
        HANDLE handle = GetClipboardData(CF_TEXT);
        if (handle != nullptr) {
            const char* data = static_cast<const char*>(GlobalLock(handle));
            if (data != nullptr) {
                text = data;
                GlobalUnlock(handle);
                ok = true;
            }
        }
    } else {
        text.clear();
        ok = true;  // an empty clipboard is a valid answer
    }
    CloseClipboard();
    if (!ok) error = "the clipboard did not expose text";
    return ok;
}

int l_setclipboard(lua_State* L) {
    require_capability(L, kClipboardKey, "setclipboard");
    std::size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    std::string error;
    if (!clipboard_set(std::string(text, length), error)) luaL_error(L, "setclipboard: %s", error.c_str());
    return 0;
}

int l_getclipboard(lua_State* L) {
    require_capability(L, kClipboardKey, "getclipboard");
    std::string text;
    std::string error;
    if (!clipboard_get(text, error)) luaL_error(L, "getclipboard: %s", error.c_str());
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

// -- identity ---------------------------------------------------------------

int l_getidentity(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "saintnova.identity");
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        lua_pushinteger(L, 7);
    }
    return 1;
}

int l_setidentity(lua_State* L) {
    const int identity = luaL_checkinteger(L, 1);
    if (identity < 0 || identity > 15) luaL_error(L, "setidentity: identity must be between 0 and 15");
    lua_pushinteger(L, identity);
    lua_setfield(L, LUA_REGISTRYINDEX, "saintnova.identity");
    return 0;
}

// -- hardware id ------------------------------------------------------------

std::string machine_guid() {
    wchar_t buffer[256] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", RRF_RT_REG_SZ,
                     nullptr, buffer, &size) == ERROR_SUCCESS) {
        return narrow(buffer);
    }
    return std::string();
}

std::string system_volume_serial() {
    wchar_t windows_dir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windows_dir, MAX_PATH) == 0) return std::string();
    std::wstring root(windows_dir, 3);  // e.g. "C:\"
    DWORD serial = 0;
    if (GetVolumeInformationW(root.c_str(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0) == FALSE) {
        return std::string();
    }
    char text[16];
    std::snprintf(text, sizeof(text), "%08lx", static_cast<unsigned long>(serial));
    return text;
}

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

int l_gethwid(lua_State* L) {
    std::string material = machine_guid();
    if (material.empty()) {
        wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        if (GetComputerNameW(name, &size) != FALSE) material = narrow(name);
    }
    if (material.empty()) luaL_error(L, "gethwid: no stable machine identifier is available");
    material += "|";
    material += system_volume_serial();

    char text[32];
    std::snprintf(text, sizeof(text), "saintnova-%016llx",
                  static_cast<unsigned long long>(fnv1a(material)));
    lua_pushstring(L, text);
    return 1;
}

// -- message box ------------------------------------------------------------

int l_messagebox(lua_State* L) {
    std::size_t text_length = 0;
    std::size_t title_length = 0;
    const char* text = luaL_checklstring(L, 1, &text_length);
    const char* title = luaL_optlstring(L, 2, "SaintNova", &title_length);
    const int flag = luaL_optinteger(L, 3, 0);

    UINT type = MB_OK;
    switch (flag) {
        case 0: type = MB_OK; break;
        case 1: type = MB_OKCANCEL; break;
        case 2: type = MB_ABORTRETRYIGNORE; break;
        case 3: type = MB_YESNOCANCEL; break;
        case 4: type = MB_YESNO; break;
        case 5: type = MB_RETRYCANCEL; break;
        case 6: type = MB_CANCELTRYCONTINUE; break;
        default: luaL_error(L, "messagebox: unsupported flag %d", flag);
    }

    const std::wstring wide_text = widen(std::string(text, text_length));
    const std::wstring wide_title = widen(std::string(title, title_length));
    const int result = MessageBoxW(nullptr, wide_text.c_str(), wide_title.c_str(), type | MB_TOPMOST | MB_SETFOREGROUND);

    int mapped = 0;
    switch (result) {
        case IDOK: mapped = 1; break;
        case IDCANCEL: mapped = 2; break;
        case IDABORT: mapped = 3; break;
        case IDRETRY: mapped = 4; break;
        case IDIGNORE: mapped = 5; break;
        case IDYES: mapped = 6; break;
        case IDNO: mapped = 7; break;
        case IDTRYAGAIN: mapped = 10; break;
        case IDCONTINUE: mapped = 11; break;
        default: luaL_error(L, "messagebox: the dialog could not be displayed (error %d)",
                                   static_cast<int>(GetLastError()));
    }
    lua_pushinteger(L, mapped);
    return 1;
}

// -- metatables and readonly state -----------------------------------------

int l_getrawmetatable(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_getmetatable(L, 1) == 0) {
        lua_pushnil(L);
        return 1;
    }
    return 1;
}

int l_setrawmetatable(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!lua_isnil(L, 2)) luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushvalue(L, 2);
    const int applied = lua_setmetatable(L, 1);
    lua_pushboolean(L, applied != 0);
    return 1;
}

int l_isreadonly(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushboolean(L, lua_getreadonly(L, 1) != 0);
    return 1;
}

int l_setreadonly(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const bool readonly = lua_toboolean(L, 2) != 0;
    lua_setreadonly(L, 1, readonly ? 1 : 0);
    return 0;
}

int l_makereadonly(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_setreadonly(L, 1, 1);
    return 0;
}

int l_makewritable(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_setreadonly(L, 1, 0);
    return 0;
}

int l_iswritable(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushboolean(L, lua_getreadonly(L, 1) == 0);
    return 1;
}

// -- closures ---------------------------------------------------------------

int l_iscclosure(lua_State* L) {
    lua_pushboolean(L, lua_iscfunction(L, 1) != 0);
    return 1;
}

int l_islclosure(lua_State* L) {
    lua_pushboolean(L, lua_isfunction(L, 1) != 0 && lua_iscfunction(L, 1) == 0);
    return 1;
}

int newcclosure_trampoline(lua_State* L) {
    const int nargs = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, nargs, LUA_MULTRET);
    return lua_gettop(L);
}

int l_newcclosure(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, newcclosure_trampoline, "newcclosure", 1);
    const int wrapper_index = lua_gettop(L);

    lua_getfield(L, LUA_REGISTRYINDEX, kNewClosuresKey);
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, wrapper_index);
        lua_pushvalue(L, 1);
        lua_rawset(L, -3);
    }
    lua_pop(L, 1);
    return 1;
}

int l_clonefunction(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (lua_iscfunction(L, 1) != 0) luaL_error(L, "clonefunction: cannot clone a C closure");
    lua_clonefunction(L, 1);
    return 1;
}

int l_getcallbackvalue(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, kNewClosuresKey);
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, 1);
        lua_rawget(L, -2);
        if (!lua_isnil(L, -1)) return 1;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    luaL_error(L, "getcallbackvalue: function was not created by newcclosure");
}

// -- upvalues ---------------------------------------------------------------
//
// Luau's debug library exposes frames but not upvalues, so these back the
// debug.getupvalues/getupvalue/setupvalue surface documented by Wave.

int l_upvalue_count(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_newtable(L);
    int count = 0;
    while (true) {
        const char* name = lua_getupvalue(L, 1, count + 1);
        if (name == nullptr) break;
        ++count;
        lua_rawseti(L, -2, count);
    }
    lua_pushinteger(L, count);
    return 2;
}

int l_upvalue_get(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    const int index = luaL_checkinteger(L, 2);
    const char* name = lua_getupvalue(L, 1, index);
    if (name == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, name);
    return 2;  // value, name
}

int l_upvalue_set(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    const int index = luaL_checkinteger(L, 2);
    lua_settop(L, 3);
    lua_pushvalue(L, 3);
    const char* name = lua_setupvalue(L, 1, index);
    if (name == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, name);
    return 1;
}

// -- registration -----------------------------------------------------------

const luaL_Reg kFunctions[] = {
    {"readfile", l_readfile},
    {"writefile", l_writefile},
    {"appendfile", l_appendfile},
    {"isfile", l_isfile},
    {"isfolder", l_isfolder},
    {"makefolder", l_makefolder},
    {"delfile", l_delfile},
    {"deletefile", l_delfile},
    {"delfolder", l_delfolder},
    {"deletefolder", l_delfolder},
    {"listfiles", l_listfiles},
    {"loadfile", l_loadfile},

    {"setclipboard", l_setclipboard},
    {"toclipboaard", l_setclipboard},
    {"setrbxclipboard", l_setclipboard},
    {"getclipboard", l_getclipboard},
    {"fromclipboard", l_getclipboard},

    {"getidentity", l_getidentity},
    {"getthreadidentity", l_getidentity},
    {"getcontext", l_getidentity},
    {"getthreadcontext", l_getidentity},
    {"get_thread_context", l_getidentity},
    {"get_thread_identity", l_getidentity},
    {"setidentity", l_setidentity},
    {"setthreadidentity", l_setidentity},
    {"setcontext", l_setidentity},
    {"setthreadcontext", l_setidentity},
    {"set_thread_context", l_setidentity},
    {"set_thread_identity", l_setidentity},

    {"gethwid", l_gethwid},
    {"get_hwid", l_gethwid},
    {"messagebox", l_messagebox},
    {"messageboxasync", l_messagebox},

    {"getrawmetatable", l_getrawmetatable},
    {"setrawmetatable", l_setrawmetatable},
    {"isreadonly", l_isreadonly},
    {"setreadonly", l_setreadonly},
    {"makereadonly", l_makereadonly},
    {"make_readonly", l_makereadonly},
    {"makewritable", l_makewritable},
    {"make_writable", l_makewritable},
    {"make_writeable", l_makewritable},
    {"makewriteable", l_makewritable},
    {"iswritable", l_iswritable},
    {"iswriteable", l_iswritable},

    {"iscclosure", l_iscclosure},
    {"is_c_closure", l_iscclosure},
    {"islclosure", l_islclosure},
    {"is_l_closure", l_islclosure},
    {"newcclosure", l_newcclosure},
    {"clonefunction", l_clonefunction},
    {"clonefunc", l_clonefunction},
    {"getcallbackvalue", l_getcallbackvalue},
    {nullptr, nullptr},
};

}  // namespace

std::string default_workspace_dir() {
    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    fs::path root;
    if (length > 0 && length < MAX_PATH) {
        root = fs::path(buffer) / L"SaintNova" / L"workspace";
    } else {
        wchar_t module[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, module, MAX_PATH) == 0) return std::string();
        root = fs::path(module).parent_path() / L"workspace";
    }
    return utf8_of(root);
}

void install_harness_api(lua_State* L, const HarnessApiOptions& options) {
    std::string workspace = options.workspace_dir;
    if (workspace.empty()) workspace = default_workspace_dir();
    std::error_code ec;
    fs::create_directories(path_from_utf8(workspace), ec);

    lua_pushlstring(L, workspace.data(), workspace.size());
    lua_setfield(L, LUA_REGISTRYINDEX, kWorkspaceKey);

    lua_pushboolean(L, options.enable_filesystem ? 1 : 0);
    lua_setfield(L, LUA_REGISTRYINDEX, kFilesystemKey);
    lua_pushboolean(L, options.enable_clipboard ? 1 : 0);
    lua_setfield(L, LUA_REGISTRYINDEX, kClipboardKey);

    lua_pushinteger(L, 7);
    lua_setfield(L, LUA_REGISTRYINDEX, "saintnova.identity");

    // Weak-keyed map from newcclosure wrappers to their original function.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushliteral(L, "k");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, kNewClosuresKey);

    for (const luaL_Reg* reg = kFunctions; reg->name != nullptr; ++reg) {
        lua_pushcfunction(L, reg->func, reg->name);
        lua_setglobal(L, reg->name);
    }

    // Reflection helpers stay on the private __saintnova table rather than in the
    // global namespace; the prelude wraps them in the documented debug API.
    lua_getglobal(L, "__saintnova");
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, l_upvalue_count, "upvalues");
        lua_setfield(L, -2, "upvalues");
        lua_pushcfunction(L, l_upvalue_get, "getupvalue");
        lua_setfield(L, -2, "getupvalue");
        lua_pushcfunction(L, l_upvalue_set, "setupvalue");
        lua_setfield(L, -2, "setupvalue");
    }
    lua_pop(L, 1);
}

}  // namespace saintnova
