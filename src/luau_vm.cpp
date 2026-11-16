#include "saintnova/luau_vm.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lua.h"
#include "lualib.h"

#include "Luau/Compiler.h"

#include "saintnova/crypt_api.hpp"
#include "saintnova/harness_api.hpp"

namespace saintnova {
extern const std::string_view kSandboxSource;
}

namespace saintnova {

namespace {

using Clock = std::chrono::steady_clock;

double seconds_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

}  // namespace

struct LuauVm::Impl {
    lua_State* L = nullptr;
    std::string init_error;
    LuauVm::Options options;

    bool active = false;
    bool aborting = false;
    bool timed_out = false;
    bool cancelled = false;
    std::string abort_message;

    std::atomic<bool>* self_cancel = nullptr;
    std::atomic<bool>* external_cancel = nullptr;

    const OutputSink* sink = nullptr;
    std::size_t emitted = 0;
    std::size_t max_output = 2000;
    std::vector<OutputLine> collected;

    Clock::time_point started;
    Clock::time_point deadline;
    std::uint32_t timeout_ms = 5000;

    struct Pending {
        lua_State* co = nullptr;
        double resume_at = 0.0;
    };

    std::vector<Pending> pending;
    std::unordered_map<lua_State*, int> refs;
    std::unordered_set<lua_State*> dead;

    lua_State* main_co = nullptr;
    std::string main_error;

    // -- output ------------------------------------------------------------
    void emit(LogLevel level, std::string source, std::string text) {
        if (emitted >= max_output) return;
        ++emitted;
        OutputLine line{level, std::move(source), std::move(text)};
        if (sink != nullptr && *sink) (*sink)(line);
        collected.push_back(std::move(line));
    }

    // -- thread anchoring --------------------------------------------------
    // Anchors the thread value on top of `from` in the registry so the GC keeps
    // it alive, then pops it.
    void anchor(lua_State* from) {
        lua_State* co = lua_tothread(from, -1);
        if (co == nullptr) return;
        int ref = lua_ref(from, -1);
        refs[co] = ref;
        lua_pop(from, 1);
    }

    void release(lua_State* co) {
        auto it = refs.find(co);
        if (it == refs.end()) return;
        lua_unref(L, it->second);
        refs.erase(it);
    }

    double elapsed() const { return seconds_between(started, Clock::now()); }

    // -- resume plumbing ---------------------------------------------------
    double take_yield_delay(lua_State* co) {
        int n = lua_gettop(co);
        if (n <= 0) return 0.0;
        double delay = 0.0;
        if (lua_isnumber(co, -n)) delay = lua_tonumber(co, -n);
        if (!std::isfinite(delay) || delay < 0.0) delay = 0.0;
        lua_pop(co, n);
        return delay;
    }

    void finish_thread(lua_State* co) {
        release(co);
        dead.erase(co);
    }

    void handle_resume(lua_State* co, int status) {
        if (status == 0) {  // LUA_OK
            finish_thread(co);
            return;
        }
        if (status == LUA_YIELD) {
            double delay = take_yield_delay(co);
            pending.push_back(Pending{co, elapsed() + delay});
            return;
        }

        std::string message;
        const char* raw = lua_tostring(co, -1);
        if (raw != nullptr) {
            message = raw;
        } else {
            message = "script error (non-string error object)";
        }
        lua_pop(co, 1);

        if (co == main_co) {
            main_error = message;
        } else {
            emit(LogLevel::Error, "task", message);
        }
        finish_thread(co);
    }

    // Creates a coroutine from the function at `index` on `caller`, moves
    // `index+1 .. top` into it, and returns the new thread (anchored).
    lua_State* make_thread(lua_State* caller, int index) {
        int nargs = lua_gettop(caller) - index;
        lua_State* co = lua_newthread(caller);
        anchor(caller);
        lua_pushvalue(caller, index);
        for (int i = 1; i <= nargs; ++i) lua_pushvalue(caller, index + i);
        lua_xmove(caller, co, nargs + 1);
        return co;
    }

    int resume_now(lua_State* caller, lua_State* co, int nargs, bool report_errors) {
        int status = lua_resume(co, caller, nargs);
        if (report_errors) {
            handle_resume(co, status);
        }
        return status;
    }

    void pump() {
        while (!pending.empty() && !aborting) {
            double now = elapsed();
            int pick = -1;
            for (int i = 0; i < static_cast<int>(pending.size()); ++i) {
                if (dead.count(pending[i].co) != 0) {
                    pick = i;  // handled below as a discard
                    break;
                }
                if (pending[i].resume_at <= now) {
                    pick = i;
                    break;
                }
            }

            if (pick < 0) {
                double next = pending.front().resume_at;
                for (const Pending& p : pending) next = std::min(next, p.resume_at);
                double wait_seconds = std::max(0.0, next - now);
                // Poll frequently so cancellation/timeouts stay responsive even
                // when a script sleeps for a long time.
                wait_seconds = std::min(wait_seconds, 0.02);
                if (wait_seconds > 0.0) {
                    std::this_thread::sleep_for(std::chrono::duration<double>(wait_seconds));
                }
                continue;
            }

            Pending task = pending[pick];
            pending.erase(pending.begin() + pick);

            if (dead.count(task.co) != 0) {
                finish_thread(task.co);
                continue;
            }

            int status = lua_resume(task.co, L, 0);
            handle_resume(task.co, status);
        }
    }

    // -- setup -------------------------------------------------------------
    bool init(std::string& error);
    void close();
};

namespace {

LuauVm::Impl* impl_of(lua_State* L) {
    return static_cast<LuauVm::Impl*>(lua_callbacks(L)->userdata);
}

void register_globals(lua_State* L, const luaL_Reg* regs) {
    for (; regs->name != nullptr; ++regs) {
        lua_pushcfunction(L, regs->func, regs->name);
        lua_setglobal(L, regs->name);
    }
}

void register_table(lua_State* L, const char* name, const luaL_Reg* regs) {
    lua_newtable(L);
    for (; regs->name != nullptr; ++regs) {
        lua_pushcfunction(L, regs->func, regs->name);
        lua_setfield(L, -2, regs->name);
    }
    lua_setglobal(L, name);
}

int saintnova_print(lua_State* L) {
    LuauVm::Impl* impl = impl_of(L);
    std::string text;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        std::size_t len = 0;
        const char* piece = luaL_tolstring(L, i, &len);
        if (i > 1) text.push_back('\t');
        if (piece != nullptr) text.append(piece, len);
        lua_pop(L, 1);
    }
    impl->emit(LogLevel::Info, "print", std::move(text));
    return 0;
}

int saintnova_warn(lua_State* L) {
    LuauVm::Impl* impl = impl_of(L);
    std::string text;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        std::size_t len = 0;
        const char* piece = luaL_tolstring(L, i, &len);
        if (i > 1) text.push_back('\t');
        if (piece != nullptr) text.append(piece, len);
        lua_pop(L, 1);
    }
    impl->emit(LogLevel::Warn, "warn", std::move(text));
    return 0;
}

int saintnova_loadstring(lua_State* L) {
    std::size_t length = 0;
    const char* source = luaL_checklstring(L, 1, &length);
    const char* chunk = luaL_optstring(L, 2, "=loadstring");

    std::string bytecode;
    std::string error;
    if (!LuauVm::compile_to_bytecode(std::string(source, length), bytecode, error)) {
        lua_pushnil(L);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    if (luau_load(L, chunk, bytecode.data(), bytecode.size(), 0) != 0) {
        const char* load_error = lua_tostring(L, -1);
        std::string message = load_error ? load_error : "failed to load chunk";
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, message.c_str());
        return 2;
    }
    return 1;
}

int saintnova_getgenv(lua_State* L) {
    // SaintNova runs every script in one shared global environment, so the
    // executor environment is the global table itself. Writes through the
    // returned table are visible to subsequent scripts.
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    return 1;
}

int saintnova_emit(lua_State* L) {
    LuauVm::Impl* impl = impl_of(L);
    std::size_t level_length = 0;
    const char* level_text = luaL_checklstring(L, 1, &level_length);
    std::size_t text_length = 0;
    const char* text = luaL_checklstring(L, 2, &text_length);
    const char* source = luaL_optstring(L, 3, "console");

    const LogLevel level =
        log_level_from_string(std::string_view(level_text, level_length), LogLevel::Info);
    impl->emit(level, source, std::string(text, text_length));
    return 0;
}

int saintnova_bytecode(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "saintnova.bytecode");
    return 1;
}

int saintnova_identifyexecutor(lua_State* L) {
    lua_pushstring(L, "SaintNova");
    lua_pushstring(L, SAINTNOVA_VERSION);
    return 2;
}

int saintnova_task_wait(lua_State* L) {
    double seconds = luaL_optnumber(L, 1, 0.0);
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    lua_pushnumber(L, seconds);
    return lua_yield(L, 1);
}

int saintnova_task_spawn(lua_State* L) {
    LuauVm::Impl* impl = impl_of(L);
    if (!lua_isfunction(L, 1)) {
        luaL_error(L, "task.spawn expects a function");
        return 0;
    }
    int nargs = lua_gettop(L) - 1;
    lua_State* co = impl->make_thread(L, 1);
    impl->resume_now(L, co, nargs, true);
    lua_pushlightuserdata(L, co);
    return 1;
}

int schedule_task(lua_State* L, int function_index, double delay) {
    LuauVm::Impl* impl = impl_of(L);
    if (!lua_isfunction(L, function_index)) {
        luaL_error(L, "task scheduler expects a function");
        return 0;
    }
    lua_State* co = impl->make_thread(L, function_index);
    impl->pending.push_back(LuauVm::Impl::Pending{co, impl->elapsed() + std::max(0.0, delay)});
    lua_pushlightuserdata(L, co);
    return 1;
}

int saintnova_task_defer(lua_State* L) {
    return schedule_task(L, 1, 0.0);
}

int saintnova_task_delay(lua_State* L) {
    double seconds = luaL_checknumber(L, 1);
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    return schedule_task(L, 2, seconds);
}

int saintnova_task_cancel(lua_State* L) {
    LuauVm::Impl* impl = impl_of(L);
    lua_State* co = static_cast<lua_State*>(lua_tolightuserdata(L, 1));
    if (co != nullptr) impl->dead.insert(co);
    return 0;
}

int saintnova_noop(lua_State*) {
    return 0;
}

const luaL_Reg kGlobalFunctions[] = {
    {"print", saintnova_print},
    {"warn", saintnova_warn},
    {"loadstring", saintnova_loadstring},
    {"getgenv", saintnova_getgenv},
    {"getrenv", saintnova_getgenv},
    {"identifyexecutor", saintnova_identifyexecutor},
    {"gethui", saintnova_noop},
    {nullptr, nullptr},
};

const luaL_Reg kTaskFunctions[] = {
    {"wait", saintnova_task_wait},
    {"spawn", saintnova_task_spawn},
    {"defer", saintnova_task_defer},
    {"delay", saintnova_task_delay},
    {"cancel", saintnova_task_cancel},
    {nullptr, nullptr},
};

void saintnova_interrupt(lua_State* L, int gc) {
    if (gc >= 0) return;  // garbage collector safepoint, not a script step
    LuauVm::Impl* impl = impl_of(L);
    if (impl == nullptr || !impl->active || impl->aborting) return;

    bool external = impl->external_cancel != nullptr && impl->external_cancel->load(std::memory_order_relaxed);
    bool self = impl->self_cancel != nullptr && impl->self_cancel->load(std::memory_order_relaxed);

    if (external || self) {
        impl->aborting = true;
        impl->cancelled = true;
        impl->abort_message = "execution cancelled";
        luaL_error(L, "%s", impl->abort_message.c_str());
    }

    if (Clock::now() > impl->deadline) {
        impl->aborting = true;
        impl->timed_out = true;
        impl->abort_message = "execution timed out";
        luaL_error(L, "%s after %u ms", impl->abort_message.c_str(), impl->timeout_ms);
    }
}

}  // namespace

bool LuauVm::Impl::init(std::string& error) {
    L = luaL_newstate();
    if (L == nullptr) {
        error = "could not create Luau state";
        return false;
    }
    luaL_openlibs(L);

    lua_callbacks(L)->userdata = this;
    lua_callbacks(L)->interrupt = saintnova_interrupt;

    // _G mirrors the globals table so scripts that read or write it behave the
    // same way they do in a live client.
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setglobal(L, "_G");

    lua_newtable(L);
    lua_setglobal(L, "shared");

    register_globals(L, kGlobalFunctions);
    register_table(L, "task", kTaskFunctions);

    // Legacy aliases still found in older scripts.
    lua_getglobal(L, "task");
    lua_getfield(L, -1, "wait");
    lua_setglobal(L, "wait");
    lua_getfield(L, -1, "spawn");
    lua_setglobal(L, "spawn");
    lua_getfield(L, -1, "delay");
    lua_setglobal(L, "delay");
    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushcfunction(L, saintnova_emit, "emit");
    lua_setfield(L, -2, "emit");
    lua_pushcfunction(L, saintnova_bytecode, "bytecode");
    lua_setfield(L, -2, "bytecode");
    lua_setglobal(L, "__saintnova");

    HarnessApiOptions api;
    api.workspace_dir = options.workspace_dir;
    api.enable_filesystem = options.enable_filesystem;
    api.enable_clipboard = options.enable_clipboard;
    install_harness_api(L, api);
    install_crypt_api(L);

    std::string bytecode;
    std::string compile_error;
    if (!LuauVm::compile_to_bytecode(std::string(kSandboxSource), bytecode, compile_error)) {
        error = "sandbox prelude failed to compile: " + compile_error;
        return false;
    }
    if (luau_load(L, "=saintnova.sandbox", bytecode.data(), bytecode.size(), 0) != 0) {
        const char* message = lua_tostring(L, -1);
        error = std::string("sandbox prelude failed to load: ") + (message ? message : "unknown");
        return false;
    }
    if (lua_pcall(L, 0, 0, 0) != 0) {
        const char* message = lua_tostring(L, -1);
        error = std::string("sandbox prelude failed: ") + (message ? message : "unknown");
        return false;
    }

    return true;
}

void LuauVm::Impl::close() {
    pending.clear();
    refs.clear();
    dead.clear();
    main_co = nullptr;
    if (L != nullptr) {
        lua_close(L);
        L = nullptr;
    }
}

LuauVm::LuauVm() : LuauVm(Options{}) {}

LuauVm::LuauVm(Options options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
    impl_->max_output = impl_->options.max_output_lines;
    std::string error;
    if (!impl_->init(error)) impl_->init_error = std::move(error);
}

LuauVm::~LuauVm() { impl_->close(); }

void LuauVm::cancel() { cancel_ = true; }

void LuauVm::reset() {
    impl_->close();
    impl_->init_error.clear();
    std::string error;
    if (!impl_->init(error)) impl_->init_error = std::move(error);
}

bool LuauVm::compile_to_bytecode(const std::string& source, std::string& bytecode, std::string& error) {
    Luau::CompileOptions options;
    options.optimizationLevel = 1;
    // Level 2 retains local and upvalue names, which debug.getupvalue and
    // debug.getinfo report; level 1 keeps only line information.
    options.debugLevel = 2;

    std::string result = Luau::compile(source, options);
    if (result.empty()) {
        error = "compiler produced no output";
        return false;
    }
    if (result[0] == 0) {
        error = std::string(result.c_str() + 1);
        return false;
    }
    bytecode = std::move(result);
    return true;
}

ExecutionResult LuauVm::run(const ExecutionRequest& request,
                            const OutputSink& sink,
                            std::atomic<bool>* external_cancel) {
    ExecutionResult result;
    result.id = request.id;

    Impl& vm = *impl_;
    if (!vm.init_error.empty()) {
        result.error = vm.init_error;
        return result;
    }

    // Reset per-run bookkeeping. Globals survive between runs by design: a real
    // executor shares them too, and it is what makes `loadstring` + shared state
    // behave the way script authors expect.
    vm.collected.clear();
    vm.emitted = 0;
    vm.max_output = vm.options.max_output_lines;
    vm.aborting = false;
    vm.timed_out = false;
    vm.cancelled = false;
    vm.abort_message.clear();
    vm.pending.clear();
    vm.dead.clear();
    vm.main_error.clear();
    vm.sink = &sink;
    vm.external_cancel = external_cancel;
    vm.self_cancel = &cancel_;
    vm.timeout_ms = request.timeout_ms == 0 ? 5000 : request.timeout_ms;
    vm.started = Clock::now();
    vm.deadline = vm.started + std::chrono::milliseconds(vm.timeout_ms);
    cancel_ = false;

    std::string bytecode;
    std::string compile_error;
    if (!compile_to_bytecode(request.script, bytecode, compile_error)) {
        result.error = compile_error;
        result.duration_us = static_cast<std::uint64_t>(seconds_between(vm.started, Clock::now()) * 1e6);
        vm.active = false;
        return result;
    }

    lua_pushlstring(vm.L, bytecode.data(), bytecode.size());
    lua_setfield(vm.L, LUA_REGISTRYINDEX, "saintnova.bytecode");

    lua_State* co = lua_newthread(vm.L);
    vm.anchor(vm.L);
    vm.main_co = co;

    vm.active = true;
    if (luau_load(co, request.chunk_name.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
        const char* message = lua_tostring(co, -1);
        vm.main_error = message ? message : "chunk failed to load";
        lua_pop(co, 1);
        vm.active = false;
        vm.finish_thread(co);
        vm.main_co = nullptr;
        result.error = vm.main_error;
        result.duration_us = static_cast<std::uint64_t>(seconds_between(vm.started, Clock::now()) * 1e6);
        return result;
    }

    int status = lua_resume(co, vm.L, 0);
    vm.handle_resume(co, status);

    if (!vm.aborting) vm.pump();
    vm.active = false;
    vm.main_co = nullptr;

    result.ok = vm.main_error.empty() && !vm.aborting;
    result.duration_us = static_cast<std::uint64_t>(seconds_between(vm.started, Clock::now()) * 1e6);
    if (!vm.main_error.empty()) result.error = vm.main_error;
    else if (vm.timed_out) result.error = vm.abort_message + " after " + std::to_string(vm.timeout_ms) + " ms";
    else if (vm.cancelled) result.error = vm.abort_message;
    result.output = std::move(vm.collected);

    // A new execution starts clean; queued tasks from the previous run are not
    // carried over.
    vm.pending.clear();
    vm.dead.clear();
    return result;
}

}  // namespace saintnova
