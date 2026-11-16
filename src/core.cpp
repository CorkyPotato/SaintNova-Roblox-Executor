#include "saintnova/core.hpp"

#include "saintnova/harness_backend.hpp"
#include "saintnova/roblox_backend.hpp"

namespace saintnova {

ExecutorCore::ExecutorCore() = default;
ExecutorCore::~ExecutorCore() = default;

void ExecutorCore::register_backend(std::unique_ptr<IExecutionBackend> backend) {
    if (!backend) return;
    const std::string id = backend->info().id;
    std::lock_guard<std::mutex> lock(mutex_);
    if (backends_.empty()) default_backend_ = id;
    backends_[id] = std::move(backend);
}

std::vector<BackendInfo> ExecutorCore::backends() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BackendInfo> out;
    out.reserve(backends_.size());
    for (const auto& entry : backends_) out.push_back(entry.second->info());
    return out;
}

IExecutionBackend* ExecutorCore::find_backend(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = backends_.find(id);
    return it == backends_.end() ? nullptr : it->second.get();
}

ExecutionResult ExecutorCore::execute(const ExecutionRequest& request,
                                      const std::string& backend_id,
                                      const OutputSink& sink,
                                      std::atomic<bool>* cancel) {
    const std::string id = backend_id.empty() ? [&] {
        std::lock_guard<std::mutex> lock(mutex_);
        return default_backend_;
    }() : backend_id;

    IExecutionBackend* backend = find_backend(id);
    if (backend == nullptr) {
        ExecutionResult result;
        result.id = request.id;
        result.error = "unknown backend '" + id + "'";
        OutputLine line{LogLevel::Error, "core", result.error};
        if (sink) sink(line);
        result.output.push_back(std::move(line));
        return result;
    }

    BackendInfo info = backend->info();
    if (!info.available) {
        ExecutionResult result;
        result.id = request.id;
        result.error = "backend '" + id + "' is unavailable: " + info.detail;
        OutputLine line{LogLevel::Error, "core", result.error};
        if (sink) sink(line);
        result.output.push_back(std::move(line));
        return result;
    }

    return backend->execute(request, sink, cancel);
}

void ExecutorCore::cancel(const std::string& backend_id) {
    std::vector<IExecutionBackend*> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : backends_) {
            if (backend_id.empty() || entry.first == backend_id) targets.push_back(entry.second.get());
        }
    }
    for (auto* backend : targets) backend->cancel();
}

void ExecutorCore::reset(const std::string& backend_id) {
    std::vector<IExecutionBackend*> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : backends_) {
            if (backend_id.empty() || entry.first == backend_id) targets.push_back(entry.second.get());
        }
    }
    for (auto* backend : targets) backend->reset();
}

std::unique_ptr<ExecutorCore> make_default_core() {
    auto core = std::make_unique<ExecutorCore>();
    core->register_backend(std::make_unique<HarnessBackend>());
    core->register_backend(std::make_unique<RobloxBackend>());
    return core;
}

}  // namespace saintnova
