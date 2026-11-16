#include "saintnova/roblox_backend.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace saintnova {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kTemplateLength = 16384;
constexpr char kMarker[] = "--SAINTNOVA";
constexpr std::size_t kMarkerLength = sizeof(kMarker) - 1;

constexpr std::uint8_t kTagNumber = 3;
constexpr std::uint8_t kTagString = 5;
constexpr std::uint8_t kTagTable = 6;

struct Region {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

std::vector<DWORD> studio_pids() {
    std::vector<DWORD> pids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"RobloxStudioBeta.exe") == 0) pids.push_back(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pids;
}

class Target {
public:
    Target() = default;
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;
    ~Target() { close(); }

    bool open(DWORD pid) {
        close();
        HANDLE handle = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
        if (handle == nullptr) return false;
        handle_ = handle;
        pid_ = pid;

        std::uint64_t address = 0;
        MEMORY_BASIC_INFORMATION info{};
        while (VirtualQueryEx(handle_, reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
            const std::uint64_t base = reinterpret_cast<std::uint64_t>(info.BaseAddress);
            const std::uint64_t size = info.RegionSize;
            const DWORD protect = info.Protect;
            const bool readable = info.State == MEM_COMMIT &&
                                  (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0 &&
                                  (protect & PAGE_GUARD) == 0 && (protect & PAGE_NOACCESS) == 0;
            if (readable && size >= 0x100 && size <= (1ull << 30)) regions_.push_back(Region{base, size});
            if (size == 0) break;
            address = base + size;
            if (address < base) break;
        }
        return true;
    }

    void close() {
        if (handle_ != nullptr) CloseHandle(handle_);
        handle_ = nullptr;
        pid_ = 0;
        regions_.clear();
    }

    bool valid() const { return handle_ != nullptr; }
    DWORD pid() const { return pid_; }

    bool read(std::uint64_t address, void* out, std::size_t size) const {
        if (handle_ == nullptr || size == 0) return size == 0;
        SIZE_T got = 0;
        return ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address), out, size, &got) != FALSE && got == size;
    }

    bool write(std::uint64_t address, const void* data, std::size_t size) const {
        if (handle_ == nullptr) return false;
        SIZE_T written = 0;
        return WriteProcessMemory(handle_, reinterpret_cast<LPVOID>(address), data, size, &written) != FALSE &&
               written == size;
    }

    template <typename Fn>
    void scan(Fn&& fn) const {
        constexpr std::size_t kChunk = 4u << 20;
        std::vector<std::uint8_t> buffer(kChunk);
        for (const Region& region : regions_) {
            for (std::uint64_t offset = 0; offset < region.size; offset += kChunk) {
                const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, region.size - offset));
                SIZE_T got = 0;
                if (ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(region.base + offset), buffer.data(), take, &got) ==
                        FALSE ||
                    got == 0) {
                    continue;
                }
                fn(region.base + offset, buffer.data(), static_cast<std::size_t>(got));
            }
        }
    }

    template <typename Fn>
    void scan_qwords(Fn&& fn) const {
        scan([&](std::uint64_t base, const std::uint8_t* data, std::size_t size) {
            for (std::size_t index = 0; index + 8 <= size; index += 8) {
                std::uint64_t word = 0;
                std::memcpy(&word, data + index, 8);
                fn(base + index, word);
            }
        });
    }

    std::vector<std::uint64_t> find_bytes(const void* needle, std::size_t length) const {
        std::vector<std::uint64_t> hits;
        const auto* pattern = static_cast<const std::uint8_t*>(needle);
        scan([&](std::uint64_t base, const std::uint8_t* data, std::size_t size) {
            if (size < length) return;
            const std::uint8_t* cursor = data;
            const std::uint8_t* end = data + size;
            while (cursor + length <= end) {
                const void* found = std::memchr(cursor, pattern[0], static_cast<std::size_t>(end - cursor - length + 1));
                if (found == nullptr) break;
                const auto* at = static_cast<const std::uint8_t*>(found);
                if (std::memcmp(at, pattern, length) == 0) hits.push_back(base + static_cast<std::size_t>(at - data));
                cursor = at + 1;
            }
        });
        return hits;
    }

    bool contains(std::uint64_t address, std::size_t size) const {
        for (const Region& region : regions_) {
            if (address >= region.base && address + size <= region.base + region.size) return true;
        }
        return false;
    }

private:
    HANDLE handle_ = nullptr;
    DWORD pid_ = 0;
    std::vector<Region> regions_;
};

bool read_tstring(const Target& target, std::uint64_t object, std::string& out) {
    std::uint8_t header = 0;
    std::uint32_t length = 0;
    if (!target.read(object, &header, 1) || header != kTagString) return false;
    if (!target.read(object + 0x14, &length, 4) || length > 4096) return false;
    out.resize(length);
    if (length != 0 && !target.read(object + 0x18, out.data(), length)) return false;
    return true;
}

bool read_lua_string(const Target& target, std::uint64_t slot, std::string& out) {
    std::uint64_t payload = 0;
    std::uint32_t tag = 0;
    if (!target.read(slot, &payload, 8) || !target.read(slot + 0x0C, &tag, 4)) return false;
    if ((tag & 0xF) != kTagString || payload == 0) {
        out.clear();
        return false;
    }
    return read_tstring(target, payload, out);
}

bool read_lua_number(const Target& target, std::uint64_t slot, double& value) {
    std::uint64_t bits = 0;
    std::uint32_t tag = 0;
    if (!target.read(slot, &bits, 8) || !target.read(slot + 0x0C, &tag, 4)) return false;
    if ((tag & 0xF) != kTagNumber) return false;
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

bool write_lua_number(const Target& target, std::uint64_t slot, double value) {
    std::uint8_t block[16] = {};
    std::memcpy(block, &value, sizeof(value));
    std::uint32_t tag = kTagNumber;
    std::memcpy(block + 0x0C, &tag, 4);
    return target.write(slot, block, sizeof(block));
}

struct Loader {
    std::uint64_t table = 0;
    std::uint64_t go = 0;
    std::uint64_t runs = 0;
    std::uint64_t state = 0;
    std::uint64_t result = 0;
    std::uint64_t log = 0;

    bool complete() const { return go != 0 && runs != 0 && state != 0 && result != 0 && log != 0; }
};

std::vector<std::uint64_t> find_string_objects(const Target& target, const char* text) {
    const std::size_t length = std::strlen(text);
    std::vector<std::uint64_t> objects;
    for (const std::uint64_t hit : target.find_bytes(text, length)) {
        const std::uint64_t object = hit - 0x18;
        std::uint8_t header = 0;
        std::uint32_t stored = 0;
        if (!target.read(object, &header, 1) || header != kTagString) continue;
        if (!target.read(object + 0x14, &stored, 4) || stored != length) continue;
        if (std::find(objects.begin(), objects.end(), object) == objects.end()) objects.push_back(object);
    }
    return objects;
}

std::vector<std::uint64_t> find_nodes_for(const Target& target, const std::vector<std::uint64_t>& objects) {
    std::vector<std::uint64_t> nodes;
    if (objects.empty()) return nodes;
    target.scan_qwords([&](std::uint64_t address, std::uint64_t word) {
        if (std::find(objects.begin(), objects.end(), word) == objects.end()) return;
        if (address < 0x10) return;
        const std::uint64_t node = address - 0x10;
        std::uint32_t key_tag = 0;
        if (!target.read(node + 0x1C, &key_tag, 4)) return;
        if ((key_tag & 0xF) != kTagString) return;
        if (std::find(nodes.begin(), nodes.end(), node) == nodes.end()) nodes.push_back(node);
    });
    return nodes;
}

bool walk_loader(const Target& target, std::uint64_t table, Loader& loader) {
    std::uint8_t header[0x30] = {};
    if (!target.read(table, header, sizeof(header)) || header[0] != kTagTable) return false;
    const unsigned shift = header[5];
    if (shift > 16) return false;
    const std::size_t count = static_cast<std::size_t>(1) << shift;
    std::uint64_t nodes = 0;
    std::memcpy(&nodes, header + 0x28, 8);
    if (nodes < 0x10000) return false;

    std::vector<std::uint8_t> block(count * 32);
    if (!target.read(nodes, block.data(), block.size())) return false;

    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t* entry = block.data() + index * 32;
        std::uint32_t key_tag = 0;
        std::memcpy(&key_tag, entry + 0x1C, 4);
        if ((key_tag & 0xF) != kTagString) continue;
        std::uint64_t key = 0;
        std::memcpy(&key, entry + 0x10, 8);
        std::string name;
        if (!read_tstring(target, key, name)) continue;
        const std::uint64_t slot = nodes + index * 32;
        if (name == "SaintNovaGo") loader.go = slot;
        else if (name == "SaintNovaRuns") loader.runs = slot;
        else if (name == "SaintNovaState") loader.state = slot;
        else if (name == "SaintNovaResult") loader.result = slot;
        else if (name == "SaintNovaLog") loader.log = slot;
    }
    loader.table = table;
    return loader.complete();
}

std::vector<Loader> find_loaders(const Target& target) {
    std::vector<Loader> loaders;
    const auto objects = find_string_objects(target, "SaintNovaRuns");
    const auto nodes = find_nodes_for(target, objects);
    if (nodes.empty()) return loaders;

    std::vector<std::uint64_t> tables;
    target.scan([&](std::uint64_t base, const std::uint8_t* data, std::size_t size) {
        if (size < 0x30) return;
        const std::size_t limit = size - 0x30;
        for (std::size_t index = 0; index < limit; index += 8) {
            if (data[index] != kTagTable) continue;
            const unsigned shift = data[index + 5];
            if (shift > 16) continue;
            std::uint64_t array = 0;
            std::memcpy(&array, data + index + 0x28, 8);
            if (array < 0x10000) continue;
            const std::uint64_t span = static_cast<std::uint64_t>(32) << shift;
            for (const std::uint64_t node : nodes) {
                if (node >= array && node < array + span && (node - array) % 32 == 0) {
                    tables.push_back(base + index);
                    break;
                }
            }
        }
    });

    for (const std::uint64_t table : tables) {
        Loader candidate;
        if (!walk_loader(target, table, candidate)) continue;
        const bool duplicate = std::any_of(loaders.begin(), loaders.end(), [&](const Loader& existing) {
            return existing.table == candidate.table;
        });
        if (!duplicate) loaders.push_back(candidate);
    }
    return loaders;
}

std::vector<std::uint64_t> find_template_buffers(const Target& target, bool strict) {
    std::vector<std::uint64_t> buffers;
    for (const std::uint64_t hit : target.find_bytes(kMarker, kMarkerLength)) {
        std::uint8_t tail[256] = {};
        if (!target.read(hit + kMarkerLength, tail, sizeof(tail))) continue;
        if (strict) {
            // A freshly replicated template Source is the marker followed by
            // spaces. Payload copies from an older run start with "\nr" and are
            // rejected here; the cache keeps the real buffers afterwards.
            const bool whitespace = (tail[0] == ' ' || tail[0] == '\n' || tail[0] == '\r' || tail[0] == '\t') &&
                                    (tail[1] == ' ' || tail[1] == '\n' || tail[1] == '\r' || tail[1] == '\t');
            if (!whitespace) continue;
        } else {
            if (!(tail[0] == ' ' || tail[0] == '\n' || tail[0] == '\r' || tail[0] == '\t')) continue;
        }
        bool printable = true;
        for (const std::uint8_t byte : tail) {
            const bool ok = byte == '\t' || byte == '\n' || byte == '\r' || (byte >= 0x20 && byte <= 0x7E);
            if (!ok) {
                printable = false;
                break;
            }
        }
        if (!printable) continue;
        if (!target.contains(hit, kTemplateLength)) continue;
        if (std::find(buffers.begin(), buffers.end(), hit) == buffers.end()) buffers.push_back(hit);
    }
    return buffers;
}

struct CacheEntry {
    DWORD pid = 0;
    std::uint64_t runs = 0;
    std::vector<std::uint64_t> buffers;
};

std::string cache_path() {
    char temp[MAX_PATH] = {};
    const DWORD length = GetTempPathA(MAX_PATH, temp);
    if (length == 0) return {};
    return std::string(temp) + "saintnova_backend.cache";
}

CacheEntry load_cache() {
    CacheEntry entry;
    const std::string path = cache_path();
    if (path.empty()) return entry;
    std::ifstream in(path);
    if (!in) return entry;
    std::size_t count = 0;
    if (!(in >> entry.pid >> entry.runs >> count)) return CacheEntry{};
    for (std::size_t index = 0; index < count && in; ++index) {
        std::uint64_t address = 0;
        if (in >> address) entry.buffers.push_back(address);
    }
    return entry;
}

void save_cache(const CacheEntry& entry) {
    const std::string path = cache_path();
    if (path.empty()) return;
    std::ofstream out(path, std::ios::trunc);
    out << entry.pid << ' ' << entry.runs << ' ' << entry.buffers.size() << '\n';
    for (const std::uint64_t address : entry.buffers) out << address << '\n';
}

bool buffers_valid(const Target& target, const std::vector<std::uint64_t>& buffers) {
    if (buffers.empty()) return false;
    for (const std::uint64_t buffer : buffers) {
        char header[kMarkerLength] = {};
        if (target.read(buffer, header, sizeof(header)) &&
            std::memcmp(header, kMarker, kMarkerLength) == 0) {
            return true;
        }
    }
    return false;
}

std::string studio_compile_error() {
    const char* local = std::getenv("LOCALAPPDATA");
    if (local == nullptr) return {};
    std::string pattern = std::string(local) + "\\Roblox\\logs\\*.log";
    WIN32_FIND_DATAA find{};
    HANDLE search = FindFirstFileA(pattern.c_str(), &find);
    if (search == INVALID_HANDLE_VALUE) return {};

    std::string newest;
    std::uint64_t newest_time = 0;
    do {
        const std::uint64_t stamp = (static_cast<std::uint64_t>(find.ftLastWriteTime.dwHighDateTime) << 32) |
                                    find.ftLastWriteTime.dwLowDateTime;
        if (stamp >= newest_time) {
            newest_time = stamp;
            newest = find.cFileName;
        }
    } while (FindNextFileA(search, &find));
    FindClose(search);
    if (newest.empty()) return {};

    const std::string path = std::string(local) + "\\Roblox\\logs\\" + newest;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::streamoff window = 256 * 1024;
    in.seekg(size > window ? size - window : 0, std::ios::beg);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string best;
    std::size_t position = 0;
    while (true) {
        const std::size_t found = text.find("CreatorError", position);
        if (found == std::string::npos) break;
        const std::size_t end = text.find('\n', found);
        std::string line = text.substr(found, end == std::string::npos ? std::string::npos : end - found);
        position = end == std::string::npos ? text.size() : end + 1;
        if (line.find("SaintNovaTemplate") == std::string::npos) continue;
        const std::size_t marker = line.find("Error: ");
        if (marker != std::string::npos) best = line.substr(marker + 7);
    }
    while (!best.empty() && (best.back() == '\r' || best.back() == '\n')) best.pop_back();
    return best;
}

bool loader_still_valid(const Target& target, const std::vector<Loader>& loaders, const std::vector<std::uint64_t>& buffers) {
    bool counter = false;
    for (const Loader& loader : loaders) {
        double value = 0.0;
        if (read_lua_number(target, loader.runs, value)) {
            counter = true;
            break;
        }
    }
    if (!counter) return false;
    for (const std::uint64_t buffer : buffers) {
        char header[kMarkerLength] = {};
        if (target.read(buffer, header, sizeof(header)) &&
            std::memcmp(header, kMarker, kMarkerLength) == 0) {
            return true;
        }
    }
    return false;
}

struct Session {
    Target target;
    std::vector<Loader> loaders;
    std::vector<std::uint64_t> buffers;
    Clock::time_point discovered{};
};

std::shared_ptr<Session> discover() {
    for (const DWORD pid : studio_pids()) {
        auto session = std::make_shared<Session>();
        if (!session->target.open(pid)) continue;
        session->loaders = find_loaders(session->target);
        if (session->loaders.empty()) continue;

        const std::uint64_t runs = session->loaders.front().runs;
        const CacheEntry cache = load_cache();
        if (cache.pid == pid && cache.runs == runs && buffers_valid(session->target, cache.buffers)) {
            session->buffers = cache.buffers;
        } else {
            session->buffers = find_template_buffers(session->target, true);
            if (session->buffers.empty()) session->buffers = find_template_buffers(session->target, false);
            save_cache(CacheEntry{pid, runs, session->buffers});
        }
        session->discovered = Clock::now();
        return session;
    }
    return nullptr;
}

void emit(const OutputSink& sink, ExecutionResult& result, LogLevel level, const char* source, std::string text) {
    OutputLine line{level, source, std::move(text)};
    if (sink) sink(line);
    result.output.push_back(std::move(line));
}

std::uint64_t duration_us(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}

}  // namespace

struct RobloxBackend::Impl {
    mutable std::mutex mutex;
    mutable std::shared_ptr<Session> session;
    mutable Clock::time_point session_at{};
    mutable std::string last_error;

    std::shared_ptr<Session> acquire(bool force) const {
        std::lock_guard<std::mutex> lock(mutex);
        const auto now = Clock::now();
        if (!force && session && now - session_at < std::chrono::seconds(60) &&
            loader_still_valid(session->target, session->loaders, session->buffers)) {
            return session;
        }
        auto fresh = discover();
        session = fresh;
        session_at = now;
        return fresh;
    }

    void invalidate() const {
        std::lock_guard<std::mutex> lock(mutex);
        session.reset();
    }
};

RobloxBackend::RobloxBackend() : impl_(std::make_unique<Impl>()) {}
RobloxBackend::~RobloxBackend() = default;

std::string RobloxBackend::unavailability_reason() {
    return "Roblox Studio is not running with the SaintNova loader. Open place/saintnova_client.rbxlx, "
           "press Play, and wait for SAINTNOVA-READY in the Output window.";
}

BackendInfo RobloxBackend::info() const {
    BackendInfo info;
    info.id = "roblox";
    info.kind = BackendKind::Roblox;

    const auto pids = studio_pids();
    if (pids.empty()) {
        info.name = "Roblox Studio (not running)";
        info.available = false;
        info.detail = unavailability_reason();
        return info;
    }

    const auto session = impl_->acquire(false);
    if (!session) {
        info.name = "Roblox Studio (loader not found)";
        info.available = false;
        info.detail = "Studio is running (pid " + std::to_string(pids.front()) +
                      ") but the SaintNova loader globals were not found. Start a Play session with the loader installed.";
        return info;
    }

    info.name = "Roblox Studio (live client)";
    info.available = true;
    info.detail = "Attached to pid " + std::to_string(session->target.pid()) + "; " +
                  std::to_string(session->buffers.size()) + " template buffer(s), " +
                  std::to_string(session->loaders.size()) + " loader table(s); compile+require path.";
    return info;
}

ExecutionResult RobloxBackend::execute(const ExecutionRequest& request, const OutputSink& sink,
                                       std::atomic<bool>* cancel) {
    const auto start = Clock::now();
    ExecutionResult result;
    result.id = request.id;

    auto session = impl_->acquire(false);
    if (!session) {
        result.error = unavailability_reason();
        emit(sink, result, LogLevel::Error, "backend", result.error);
        result.duration_us = duration_us(start);
        return result;
    }

    std::string source = request.script;
    if (source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB && static_cast<unsigned char>(source[2]) == 0xBF) {
        source.erase(0, 3);
    }

    std::string wrapped = std::string(kMarker) + "\nreturn function()\n" + source + "\nend\n";
    if (wrapped.size() > kTemplateLength) {
        result.error = "script is too large for the client template (" + std::to_string(wrapped.size()) + " > " +
                       std::to_string(kTemplateLength) + " bytes)";
        emit(sink, result, LogLevel::Error, "backend", result.error);
        result.duration_us = duration_us(start);
        return result;
    }
    wrapped.resize(kTemplateLength, ' ');

    if (session->buffers.empty()) {
        result.error = "SaintNovaTemplate Source buffer not found; the place has no ReplicatedStorage.SaintNovaTemplate";
        emit(sink, result, LogLevel::Error, "backend", result.error);
        result.duration_us = duration_us(start);
        return result;
    }

    bool wrote = false;
    for (const std::uint64_t buffer : session->buffers) {
        const bool ok = session->target.write(buffer, wrapped.data(), wrapped.size());
        wrote = wrote || ok;
        if (std::getenv("SAINTNOVA_DEBUG") != nullptr) {
            std::fprintf(stderr, "[debug] buffer %#llx write %s\n", static_cast<unsigned long long>(buffer),
                         ok ? "ok" : "failed");
        }
    }
    if (std::getenv("SAINTNOVA_DEBUG") != nullptr) {
        std::fprintf(stderr, "[debug] pid %lu loaders %zu buffers %zu\n", static_cast<unsigned long>(session->target.pid()),
                     session->loaders.size(), session->buffers.size());
        for (const Loader& loader : session->loaders) {
            std::fprintf(stderr, "[debug] loader table %#llx go %#llx runs %#llx state %#llx result %#llx log %#llx\n",
                         static_cast<unsigned long long>(loader.table),
                         static_cast<unsigned long long>(loader.go),
                         static_cast<unsigned long long>(loader.runs),
                         static_cast<unsigned long long>(loader.state),
                         static_cast<unsigned long long>(loader.result),
                         static_cast<unsigned long long>(loader.log));
        }
        char probe[24] = {};
        for (const std::uint64_t buffer : session->buffers) {
            const bool read_ok = session->target.read(buffer, probe, sizeof(probe));
            std::fprintf(stderr, "[debug] verify %#llx %s %.16s\n", static_cast<unsigned long long>(buffer),
                         read_ok ? "read" : "fail", read_ok ? probe : "");
        }
    }
    if (!wrote) {
        impl_->invalidate();
        result.error = "could not write the template Source buffer; re-open the place and Play again";
        emit(sink, result, LogLevel::Error, "backend", result.error);
        result.duration_us = duration_us(start);
        return result;
    }

    // Trigger immediately after the write. A delay lets the engine re-publish
    // an older Source, and the clone then compiles stale text. The environment
    // knobs exist for experiments, not for normal runs.
    int settle_ms = 0;
    if (const char* value = std::getenv("SAINTNOVA_SETTLE_MS")) {
        const int parsed = std::atoi(value);
        if (parsed >= 0 && parsed <= 10000) settle_ms = parsed;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));

    // Keep re-writing for a short window: the engine can re-publish an older
    // Source while a change is in flight, and the last write before the clone
    // is the one the module sees.
    int rewrite_ms = 0;
    if (const char* value = std::getenv("SAINTNOVA_REWRITE_MS")) {
        const int parsed = std::atoi(value);
        if (parsed >= 0 && parsed <= 5000) rewrite_ms = parsed;
    }
    for (int elapsed = 0; elapsed < rewrite_ms; elapsed += 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        for (const std::uint64_t buffer : session->buffers) {
            session->target.write(buffer, wrapped.data(), wrapped.size());
        }
    }

    struct Armed {
        Loader loader;
        double before = 0.0;
    };
    std::vector<Armed> armed;
    for (const Loader& loader : session->loaders) {
        double before = 0.0;
        if (!read_lua_number(session->target, loader.runs, before)) continue;
        armed.push_back(Armed{loader, before});
    }
    if (armed.empty()) {
        impl_->invalidate();
        result.error = "the loader run counter could not be read; re-discover on the next run";
        emit(sink, result, LogLevel::Error, "backend", result.error);
        result.duration_us = duration_us(start);
        return result;
    }

    bool triggered = false;
    for (const Armed& item : armed) {
        triggered = write_lua_number(session->target, item.loader.go, 1.0) || triggered;
    }
    if (!triggered) {
        impl_->invalidate();
        result.error = "could not trigger the loader";
        emit(sink, result, LogLevel::Error, "backend", result.error);
        result.duration_us = duration_us(start);
        return result;
    }

    const std::uint32_t timeout_ms = request.timeout_ms == 0 ? 15000 : request.timeout_ms;
    const auto deadline = start + std::chrono::milliseconds(timeout_ms);
    bool finished = false;
    Loader winner;
    while (Clock::now() < deadline) {
        if (cancel != nullptr && cancel->load()) {
            result.error = "cancelled";
            emit(sink, result, LogLevel::Error, "backend", result.error);
            result.duration_us = duration_us(start);
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        for (const Armed& item : armed) {
            double after = 0.0;
            if (!read_lua_number(session->target, item.loader.runs, after)) continue;
            if (after != item.before) {
                winner = item.loader;
                finished = true;
                break;
            }
        }
        if (finished) break;
    }

    if (!finished) {
        result.error = "timed out after " + std::to_string(timeout_ms) + " ms";
        emit(sink, result, LogLevel::Error, "runtime", result.error);
        result.duration_us = duration_us(start);
        return result;
    }

    std::string state;
    std::string value;
    std::string log;
    read_lua_string(session->target, winner.state, state);
    read_lua_string(session->target, winner.result, value);
    read_lua_string(session->target, winner.log, log);

    std::size_t position = 0;
    while (position <= log.size()) {
        const std::size_t newline = log.find('\n', position);
        const std::size_t end = newline == std::string::npos ? log.size() : newline;
        if (end > position) emit(sink, result, LogLevel::Info, "print", log.substr(position, end - position));
        if (newline == std::string::npos) break;
        position = newline + 1;
    }

    result.ok = state == "ok";
    if (!result.ok) {
        result.error = state.empty() ? "loader did not publish a state" : value;
        if (result.error.find("Requested module experienced an error") != std::string::npos) {
            const std::string detail = studio_compile_error();
            if (!detail.empty()) result.error = detail;
        }
        if (!result.error.empty()) emit(sink, result, LogLevel::Error, "runtime", result.error);
    }
    result.duration_us = duration_us(start);
    return result;
}

void RobloxBackend::cancel() { impl_->invalidate(); }

void RobloxBackend::reset() { impl_->invalidate(); }

}  // namespace saintnova
