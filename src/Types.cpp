#include "proxy/Types.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace proxy {

DateTime nowUtc() { return std::chrono::system_clock::now(); }

std::int64_t toUnixSeconds(const DateTime& value) {
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}

DateTime fromUnixSeconds(std::int64_t unixSeconds) {
    return DateTime{std::chrono::seconds(unixSeconds)};
}

std::string toIso8601(const DateTime& value) {
    const std::time_t raw = std::chrono::system_clock::to_time_t(value);
    std::tm tmUtc{};
#if defined(_WIN32)
    gmtime_s(&tmUtc, &raw);
#else
    gmtime_r(&raw, &tmUtc);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmUtc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string toString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string toString(FilterDecision decision) {
    switch (decision) {
        case FilterDecision::ALLOW:
            return "ALLOW";
        case FilterDecision::BLOCK:
            return "BLOCK";
        default:
            return "UNKNOWN";
    }
}

std::string toString(MatchType matchType) {
    switch (matchType) {
        case MatchType::EXACT:
            return "EXACT";
        case MatchType::PREFIX:
            return "PREFIX";
        case MatchType::CONTAINS:
            return "CONTAINS";
        case MatchType::REGEX:
            return "REGEX";
        default:
            return "UNKNOWN";
    }
}

}  // namespace proxy
