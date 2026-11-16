#include "saintnova/server_app.hpp"

#include <windows.h>

#include <chrono>
#include <string>

#include <nlohmann/json.hpp>

namespace saintnova {

namespace {

using json = nlohmann::json;

json line_json(const OutputLine& line, const std::string& request_id) {
    json j;
    j["id"] = request_id;
    j["level"] = std::string(to_string(line.level));
    j["source"] = line.source;
    j["text"] = line.text;
    return j;
}

json backend_json(const BackendInfo& info) {
    json j;
    j["id"] = info.id;
    j["name"] = info.name;
    j["kind"] = std::string(to_string(info.kind));
    j["available"] = info.available;
    j["detail"] = info.detail;
    return j;
}

json backends_json(const std::vector<BackendInfo>& backends) {
    json list = json::array();
    for (const auto& backend : backends) list.push_back(backend_json(backend));
    return list;
}

std::int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

SaintNovaServer::SaintNovaServer() = default;

SaintNovaServer::~SaintNovaServer() { stop(); }

bool SaintNovaServer::start(const std::string& pipe_name, std::string& error) {
    core_ = make_default_core();
    stop_requested_ = false;

    return server_.start(
        pipe_name,
        [this](PipeClient& client, const FrameHeader& header, std::string_view payload) {
            handle_frame(client, header, payload);
        },
        error);
}

void SaintNovaServer::stop() { server_.stop(); }

void SaintNovaServer::handle_frame(PipeClient& client, const FrameHeader& header, std::string_view payload) {
    const auto type = static_cast<MessageType>(header.type);

    json message = json::object();
    if (!payload.empty()) {
        try {
            message = json::parse(payload.begin(), payload.end());
        } catch (const std::exception&) {
            client.send_error("bad_payload", "payload is not valid JSON");
            return;
        }
        if (!message.is_object()) {
            client.send_error("bad_payload", "payload must be a JSON object");
            return;
        }
    }

    switch (type) {
        case MessageType::Hello: {
            json ack;
            ack["protocol"] = kProtocolVersion;
            ack["product"] = std::string(kProductName);
            ack["version"] = SAINTNOVA_VERSION;
            ack["pid"] = static_cast<int>(GetCurrentProcessId());
            ack["pipe"] = server_.name();
            ack["backends"] = backends_json(core_->backends());
            client.send(MessageType::HelloAck, ack.dump());
            break;
        }

        case MessageType::ListBackends: {
            json reply;
            reply["backends"] = backends_json(core_->backends());
            client.send(MessageType::Backends, reply.dump());
            break;
        }

        case MessageType::Execute: {
            const std::string id = message.value("id", std::string());
            if (!message.contains("script") || !message["script"].is_string()) {
                client.send_error("bad_request", "'script' must be a string", id);
                break;
            }

            ExecutionRequest request;
            request.id = id;
            request.script = message["script"].get<std::string>();
            request.chunk_name = message.value("chunk", std::string("saintnova"));
            request.timeout_ms = message.value("timeout_ms", 5000u);
            const std::string backend_id = message.value("backend", std::string());

            handle_execute(client, id, backend_id, request);
            break;
        }

        case MessageType::Cancel: {
            client.cancel_flag() = true;
            core_->cancel(message.value("backend", std::string()));
            break;
        }

        case MessageType::Reset: {
            core_->reset(message.value("backend", std::string()));
            json reply;
            reply["backends"] = backends_json(core_->backends());
            client.send(MessageType::Backends, reply.dump());
            break;
        }

        case MessageType::Ping: {
            json pong;
            pong["t"] = now_ms();
            client.send(MessageType::Pong, pong.dump());
            break;
        }

        case MessageType::Shutdown: {
            json ack;
            ack["ok"] = true;
            client.send(MessageType::Pong, ack.dump());
            stop_requested_ = true;
            break;
        }

        default: {
            client.send_error("unknown_message",
                              std::string("unsupported message type ") + message_type_name(type));
            break;
        }
    }
}

void SaintNovaServer::handle_execute(PipeClient& client,
                                  const std::string& request_id,
                                  const std::string& backend_id,
                                  const ExecutionRequest& request) {
    if (!client.try_begin_job()) {
        client.send_error("busy", "an execution is already running on this connection", request_id);
        return;
    }

    auto self = client.shared_from_this();
    client.spawn_job([this, self, request, backend_id, request_id]() {
        auto sink = [self, request_id](const OutputLine& line) {
            self->send(MessageType::Log, line_json(line, request_id).dump());
        };

        std::lock_guard<std::mutex> lock(execute_mutex_);
        const ExecutionResult result = core_->execute(request, backend_id, sink, &self->cancel_flag());

        json reply;
        reply["id"] = request_id;
        reply["ok"] = result.ok;
        reply["duration_ms"] = static_cast<double>(result.duration_us) / 1000.0;
        reply["error"] = result.error;

        json output = json::array();
        for (const auto& line : result.output) output.push_back(line_json(line, request_id));
        reply["output"] = std::move(output);

        self->send(MessageType::ExecuteResult, reply.dump());
        self->end_job();
    });
}

}  // namespace saintnova
