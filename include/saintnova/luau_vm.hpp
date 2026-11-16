#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "saintnova/common.hpp"

namespace saintnova {

// A single-threaded Luau VM with a small, deterministic task scheduler.
//
// The VM intentionally implements only what the harness needs to run real
// scripts: a curried `task` library, a captured `print`/`warn`, `loadstring`,
// and whatever the embedded sandbox prelude builds on top of those.
class LuauVm {
public:
    struct Options {
        std::uint32_t timeout_ms = 5000;
        std::size_t max_output_lines = 2000;
        bool keep_globals_between_runs = true;

        // Script-visible workspace for readfile/writefile/listfiles. An empty
        // value selects the default location under %LOCALAPPDATA%.
        std::string workspace_dir;
        bool enable_filesystem = true;
        bool enable_clipboard = true;
    };

    LuauVm();
    explicit LuauVm(Options options);
    ~LuauVm();

    LuauVm(const LuauVm&) = delete;
    LuauVm& operator=(const LuauVm&) = delete;

    ExecutionResult run(const ExecutionRequest& request,
                        const OutputSink& sink,
                        std::atomic<bool>* external_cancel = nullptr);

    // Cooperative stop for a running `run()` on the same thread.
    void cancel();

    // Recreates the VM, dropping all globals and pending tasks.
    void reset();

    // Compiles source to Luau bytecode. On failure returns false and fills
    // `error`. Used by the `compile` CLI command and by tests.
    static bool compile_to_bytecode(const std::string& source, std::string& bytecode, std::string& error);

    // Opaque VM internals. Public only so the C callbacks in luau_vm.cpp can
    // name it; nothing outside that translation unit should touch it.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> cancel_{false};
};

}  // namespace saintnova
