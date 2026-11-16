#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "saintnova/protocol.hpp"

namespace saintnova {

class PipeServer;

// One connected client. Frames arrive on the client's reader thread; writes are
// serialised so a job thread and the reader can both emit messages.
class PipeClient : public std::enable_shared_from_this<PipeClient> {
public:
    ~PipeClient();

    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    std::uint64_t id() const { return id_; }

    bool send(MessageType type, std::string_view payload);
    bool send_error(const std::string& code, const std::string& message, const std::string& request_id = {});

    // Only one execution may run per client. The core is single-threaded per
    // client so that `Cancel` and streaming logs stay coherent.
    bool try_begin_job();
    void end_job();
    void spawn_job(std::function<void()> job);
    void join_job();

    std::atomic<bool>& cancel_flag() { return cancel_; }

private:
    friend class PipeServer;

    PipeClient(PipeServer& server, std::uint64_t id, void* handle);

    void reader_loop();
    bool read_exact(void* buffer, std::size_t size);
    bool write_all(const char* data, std::size_t size);

    PipeServer& server_;
    std::uint64_t id_;
    void* handle_ = nullptr;
    std::atomic<bool> alive_{true};
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};
    std::mutex write_mutex_;
    std::thread job_;
    bool job_running_ = false;
};

class PipeServer {
public:
    using FrameHandler = std::function<void(PipeClient&, const FrameHeader&, std::string_view payload)>;
    using ConnectHandler = std::function<void(PipeClient&)>;

    PipeServer();
    ~PipeServer();

    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    // `name` is the pipe name without the \\.\pipe\ prefix, e.g. "saintnova-default".
    bool start(const std::string& name, FrameHandler handler, std::string& error);
    void stop();

    bool running() const { return running_.load(); }
    const std::string& name() const { return name_; }

    void broadcast(MessageType type, std::string_view payload);
    void set_connect_handler(ConnectHandler handler) { on_connect_ = std::move(handler); }

private:
    friend class PipeClient;

    void accept_loop();
    void client_finished(std::uint64_t id);
    void dispatch(PipeClient& client, const FrameHeader& header, std::string_view payload);
    void notify_disconnect(PipeClient& client);

    std::string name_;
    std::string full_name_;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    void* stop_event_ = nullptr;
    FrameHandler handler_;
    ConnectHandler on_connect_;

    std::mutex clients_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<PipeClient>> clients_;
    std::atomic<std::uint64_t> next_id_{1};
};

// Conventional pipe name for this machine/user.
std::string default_pipe_name();

}  // namespace saintnova
