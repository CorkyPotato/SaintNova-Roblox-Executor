#pragma once

#include <string>

struct lua_State;

namespace saintnova {

// Options for the native portion of the harness API.
struct HarnessApiOptions {
    // Root of the script-visible filesystem. All paths passed to readfile,
    // writefile, listfiles, ... resolve underneath this directory and are
    // rejected if they try to leave it.
    std::string workspace_dir;

    // Individual capabilities can be turned off for hardened embedding.
    bool enable_filesystem = true;
    bool enable_clipboard = true;
};

// Installs the native functions (filesystem, clipboard, reflection, identity,
// HWID, message box, closure helpers) and stashes the supplied options in the
// Lua registry. Must be called after luaL_openlibs and before loading the
// sandbox prelude.
void install_harness_api(lua_State* L, const HarnessApiOptions& options);

// Default workspace: %LOCALAPPDATA%\SaintNova\workspace, falling back to a
// directory beside the executable when LOCALAPPDATA is unavailable.
std::string default_workspace_dir();

}  // namespace saintnova
