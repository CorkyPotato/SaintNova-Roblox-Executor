#pragma once

#include <memory>

#include "saintnova/backend.hpp"

namespace saintnova {

// Roblox Studio backend.
//
// Attaches to RobloxStudioBeta.exe, locates the SaintNova loader globals and the
// SaintNovaTemplate Source buffer, writes the wrapped script, and reads the
// loader's state, log, and result back. It does not attach to the Roblox Player
// and contains no client-integrity bypass.
//
// A Player transport is a separate IExecutionBackend implementation: the
// contract is "run this text in that client and stream stdout back". Everything
// above this file - protocol, UI, script management, logging - stays unchanged.
// See docs/BACKENDS.md.
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
