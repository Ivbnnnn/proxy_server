#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace proxy {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

enum class FilterDecision {
    ALLOW,
    BLOCK
};

enum class MatchType {
    EXACT,
    PREFIX,
    CONTAINS,
    REGEX
};

using DateTime = std::chrono::system_clock::time_point;

DateTime nowUtc();
std::int64_t toUnixSeconds(const DateTime& value);
DateTime fromUnixSeconds(std::int64_t unixSeconds);
std::string toIso8601(const DateTime& value);
std::string toLower(std::string value);
std::string toString(LogLevel level);
std::string toString(FilterDecision decision);
std::string toString(MatchType matchType);

}  // namespace proxy
