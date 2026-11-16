#include "saintnova/common.hpp"

#include <algorithm>
#include <cctype>

namespace saintnova {

std::string_view to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "info";
}

LogLevel log_level_from_string(std::string_view text, LogLevel fallback) {
    std::string lowered;
    lowered.reserve(text.size());
    for (char c : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lowered == "trace") return LogLevel::Trace;
    if (lowered == "debug") return LogLevel::Debug;
    if (lowered == "info") return LogLevel::Info;
    if (lowered == "warn" || lowered == "warning") return LogLevel::Warn;
    if (lowered == "error") return LogLevel::Error;
    return fallback;
}

std::string_view to_string(BackendKind kind) {
    switch (kind) {
        case BackendKind::Harness: return "harness";
        case BackendKind::Roblox: return "roblox";
    }
    return "harness";
}

}  // namespace saintnova
