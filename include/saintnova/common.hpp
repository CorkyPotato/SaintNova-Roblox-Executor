#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace saintnova {

constexpr int kProtocolVersion = 1;
constexpr std::string_view kProductName = "SaintNova";

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

std::string_view to_string(LogLevel level);
LogLevel log_level_from_string(std::string_view text, LogLevel fallback = LogLevel::Info);

// A line produced by a script or by the runtime while a script is running.
struct OutputLine {
    LogLevel level = LogLevel::Info;
    std::string source;  // "print", "warn", "runtime", "system", "error"
    std::string text;
};

using OutputSink = std::function<void(const OutputLine&)>;

enum class BackendKind : std::uint8_t {
    Harness = 0,
    Roblox = 1,
};

std::string_view to_string(BackendKind kind);

struct ExecutionRequest {
    std::string id;
    std::string script;
    std::string chunk_name = "saintnova";
    std::uint32_t timeout_ms = 5000;
};

struct ExecutionResult {
    std::string id;
    bool ok = false;
    std::uint64_t duration_us = 0;
    std::string error;
    std::vector<OutputLine> output;

    void add(LogLevel level, std::string source, std::string text) {
        output.push_back(OutputLine{level, std::move(source), std::move(text)});
    }
};

struct BackendInfo {
    std::string id;
    std::string name;
    BackendKind kind = BackendKind::Harness;
    bool available = false;
    std::string detail;
};

}  // namespace saintnova
