#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "saintnova/core.hpp"
#include "saintnova/pipe_server.hpp"

namespace saintnova {

// Hosts the pipe server and translates wire messages into core calls.
//
// Both the standalone `saintnova serve` process and `saintnova.dll` use this class,
// so the embedding surface and the CLI cannot drift apart.
class SaintNovaServer {
public:
    SaintNovaServer();
    ~SaintNovaServer();

    SaintNovaServer(const SaintNovaServer&) = delete;
    SaintNovaServer& operator=(const SaintNovaServer&) = delete;

    bool start(const std::string& pipe_name, std::string& error);
    void stop();

    bool running() const { return server_.running(); }
    bool stop_requested() const { return stop_requested_.load(); }

    ExecutorCore& core() { return *core_; }
    const std::string& pipe_name() const { return server_.name(); }

private:
    void handle_frame(PipeClient& client, const FrameHeader& header, std::string_view payload);
    void handle_execute(PipeClient& client, const std::string& request_id, const std::string& backend_id,
                        const ExecutionRequest& request);

    std::unique_ptr<ExecutorCore> core_;
    PipeServer server_;
    std::atomic<bool> stop_requested_{false};

    // The harness backend owns a single Luau VM and is not re-entrant.
    // Executions from different connections are serialised here; Cancel still
    // works because it only touches atomics.
    std::mutex execute_mutex_;
};

}  // namespace saintnova
