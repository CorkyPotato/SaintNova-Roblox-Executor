#pragma once

#include <memory>

#include "saintnova/backend.hpp"

namespace saintnova {

// Live-client backend seam.
//
// This class owns no game state, no offsets and no injection primitive. It
// exists so the rest of the executor can be written and tested against a stable
// interface while the transport below it is unresolved.
//
// To make it real, implement ITransport (see docs/BACKENDS.md) and wire it in
// here: the contract is "run this text in that client and stream stdout back".
// Everything above this file - protocol, UI, script management, logging - stays
// unchanged.
class RobloxBackend final : public IExecutionBackend {
public:
    RobloxBackend();
    ~RobloxBackend() override;

    BackendInfo info() const override;
    ExecutionResult execute(const ExecutionRequest& request,
                            const OutputSink& sink,
                            std::atomic<bool>* cancel) override;
    void cancel() override;
    void reset() override;

    // Reports why the backend cannot execute yet. Never lies about availability.
    static std::string unavailability_reason();

private:
    struct Impl;
    mutable std::unique_ptr<Impl> impl_;
};

}  // namespace saintnova
