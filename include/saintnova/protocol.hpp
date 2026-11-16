#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "saintnova/common.hpp"

namespace saintnova {

// Wire format
// -----------
// Every message is framed as:
//
//   uint32 length      payload size in bytes (excludes this header)
//   uint16 type        MessageType
//   uint16 flags       reserved, must be 0
//   uint8  payload[length]   UTF-8 JSON object
//
// All integers are little-endian. Payloads are capped at kMaxPayload; a peer
// that announces a larger frame is disconnected rather than trusted.
enum class MessageType : std::uint16_t {
    Hello = 0x0001,
    HelloAck = 0x0002,
    Execute = 0x0010,
    ExecuteResult = 0x0011,
    Cancel = 0x0012,
    Log = 0x0020,
    ListBackends = 0x0030,
    Backends = 0x0031,
    Reset = 0x0032,
    Ping = 0x0040,
    Pong = 0x0041,
    Shutdown = 0x0050,
    Error = 0x00FF,
};

constexpr std::size_t kFrameHeaderSize = 8;
constexpr std::uint32_t kMaxPayload = 16u * 1024u * 1024u;

struct FrameHeader {
    std::uint32_t length = 0;
    std::uint16_t type = 0;
    std::uint16_t flags = 0;
};

// Serialises a complete frame (header + payload) into `out`.
std::string encode_frame(MessageType type, std::string_view payload, std::uint16_t flags = 0);

// Parses a header from 8 raw bytes. Returns false on reserved/invalid fields.
bool decode_header(const std::uint8_t* bytes, std::size_t size, FrameHeader& out);

const char* message_type_name(MessageType type);

}  // namespace saintnova
