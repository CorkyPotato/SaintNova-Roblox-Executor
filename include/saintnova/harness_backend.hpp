#pragma once

#include <memory>

#include "saintnova/backend.hpp"
#include "saintnova/luau_vm.hpp"

namespace saintnova {

// Local backend: runs scripts in an in-process Luau VM with a Roblox-shaped API
// surface. No process attach, no game, no injection. This is the backend the
// test suite exercises.
class HarnessBackend final : public IExecutionBackend {
public:
    explicit HarnessBackend(LuauVm::Options options = {});
    ~HarnessBackend() override;

    BackendInfo info() const override;
    ExecutionResult execute(const ExecutionRequest& request,
                            const OutputSink& sink,
                            std::atomic<bool>* cancel) override;
    void cancel() override;
    void reset() override;

private:
    std::unique_ptr<LuauVm> vm_;
};

}  // namespace saintnova
