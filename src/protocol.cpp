#include "saintnova/protocol.hpp"

#include <cstring>

namespace saintnova {

namespace {

void put_u32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
}

void put_u16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

std::uint32_t get_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t get_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

}  // namespace

std::string encode_frame(MessageType type, std::string_view payload, std::uint16_t flags) {
    std::string frame;
    frame.reserve(kFrameHeaderSize + payload.size());
    put_u32(frame, static_cast<std::uint32_t>(payload.size()));
    put_u16(frame, static_cast<std::uint16_t>(type));
    put_u16(frame, flags);
    frame.append(payload.data(), payload.size());
    return frame;
}

bool decode_header(const std::uint8_t* bytes, std::size_t size, FrameHeader& out) {
    if (size < kFrameHeaderSize || bytes == nullptr) return false;

    FrameHeader header;
    header.length = get_u32(bytes);
    header.type = get_u16(bytes + 4);
    header.flags = get_u16(bytes + 6);

    if (header.flags != 0) return false;
    if (header.length > kMaxPayload) return false;

    out = header;
    return true;
}

const char* message_type_name(MessageType type) {
    switch (type) {
        case MessageType::Hello: return "hello";
        case MessageType::HelloAck: return "hello_ack";
        case MessageType::Execute: return "execute";
        case MessageType::ExecuteResult: return "execute_result";
        case MessageType::Cancel: return "cancel";
        case MessageType::Log: return "log";
        case MessageType::ListBackends: return "list_backends";
        case MessageType::Backends: return "backends";
        case MessageType::Reset: return "reset";
        case MessageType::Ping: return "ping";
        case MessageType::Pong: return "pong";
        case MessageType::Shutdown: return "shutdown";
        case MessageType::Error: return "error";
    }
    return "unknown";
}

}  // namespace saintnova
