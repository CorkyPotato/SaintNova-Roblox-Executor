#pragma once

#include <atomic>

#include "saintnova/common.hpp"

namespace saintnova {

// A backend turns a script into observable effects.
//
// The UI talks to the core, the core talks to a backend, and a backend talks to
// whatever environment it targets. Nothing above this interface knows whether
// the script eventually runs in a local Luau VM or inside a live game client.
class IExecutionBackend {
public:
    virtual ~IExecutionBackend() = default;

    virtual BackendInfo info() const = 0;

    // Runs `request.script` to completion. Output is streamed through `sink`
    // as it is produced; the returned result repeats the full output so late
    // or disconnected clients still see it.
    //
    // `cancel` is polled cooperatively. Long-running backends must abort at a
    // safe point and return ok=false with error "cancelled".
    virtual ExecutionResult execute(const ExecutionRequest& request,
                                    const OutputSink& sink,
                                    std::atomic<bool>* cancel) = 0;

    // Asks a running execution to stop. Safe to call from another thread.
    virtual void cancel() = 0;

    // Drops persistent state (globals, sessions, handles).
    virtual void reset() {}
};

}  // namespace saintnova
