#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "saintnova/backend.hpp"

namespace saintnova {

// Owns the backend registry and routes execution requests.
//
// The core is deliberately ignorant of scripts and transports: it validates
// requests, picks a backend, forwards cancellation and normalises results.
class ExecutorCore {
public:
    ExecutorCore();
    ~ExecutorCore();

    ExecutorCore(const ExecutorCore&) = delete;
    ExecutorCore& operator=(const ExecutorCore&) = delete;

    void register_backend(std::unique_ptr<IExecutionBackend> backend);
    std::vector<BackendInfo> backends() const;

    // Returns nullptr when no backend matches.
    IExecutionBackend* find_backend(const std::string& id) const;

    // Defaults to the harness backend when `request` carries no backend id.
    ExecutionResult execute(const ExecutionRequest& request,
                            const std::string& backend_id,
                            const OutputSink& sink,
                            std::atomic<bool>* cancel);

    // Cancel/reset are scoped to one backend id; empty means every backend.
    void cancel(const std::string& backend_id);
    void reset(const std::string& backend_id);

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<IExecutionBackend>> backends_;
    std::string default_backend_;
};

// Builds the standard backend set: harness + live-client seam.
std::unique_ptr<ExecutorCore> make_default_core();

}  // namespace saintnova
