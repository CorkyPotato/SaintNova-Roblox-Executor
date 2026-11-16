#include "saintnova/harness_backend.hpp"

#include <chrono>

#include "saintnova/luau_vm.hpp"

namespace saintnova {

HarnessBackend::HarnessBackend(LuauVm::Options options)
    : vm_(std::make_unique<LuauVm>(std::move(options))) {}

HarnessBackend::~HarnessBackend() = default;

BackendInfo HarnessBackend::info() const {
    BackendInfo info;
    info.id = "harness";
    info.name = "Local Luau harness";
    info.kind = BackendKind::Harness;
    info.available = true;
    info.detail = "In-process Luau 0.747 VM with a Roblox-shaped API subset. No game process is touched.";
    return info;
}

ExecutionResult HarnessBackend::execute(const ExecutionRequest& request,
                                        const OutputSink& sink,
                                        std::atomic<bool>* cancel) {
    return vm_->run(request, sink, cancel);
}

void HarnessBackend::cancel() { vm_->cancel(); }

void HarnessBackend::reset() { vm_->reset(); }

}  // namespace saintnova
