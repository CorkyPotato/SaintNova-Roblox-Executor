#include <windows.h>

#include <memory>
#include <mutex>
#include <string>

#include "saintnova/server_app.hpp"

namespace {

std::mutex g_mutex;
std::unique_ptr<saintnova::SaintNovaServer> g_server;
std::string g_last_error;

}  // namespace

extern "C" __declspec(dllexport) const char* saintnova_version() {
    return SAINTNOVA_VERSION;
}

extern "C" __declspec(dllexport) const char* saintnova_last_error() {
    return g_last_error.c_str();
}

// Starts the pipe server on a background thread. Returns 0 on success, 1 if the
// server could not start, 2 if one is already running.
extern "C" __declspec(dllexport) int saintnova_start(const char* pipe_name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_server) {
        g_last_error = "server already running";
        return 2;
    }

    auto server = std::make_unique<saintnova::SaintNovaServer>();
    const std::string name = (pipe_name != nullptr && *pipe_name != '\0') ? pipe_name : saintnova::default_pipe_name();

    std::string error;
    if (!server->start(name, error)) {
        g_last_error = error;
        return 1;
    }

    g_server = std::move(server);
    g_last_error.clear();
    return 0;
}

extern "C" __declspec(dllexport) void saintnova_stop() {
    std::unique_ptr<saintnova::SaintNovaServer> server;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        server = std::move(g_server);
    }
    if (server) server->stop();
}
