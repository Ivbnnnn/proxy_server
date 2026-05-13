#include "proxy/AppLogger.h"

#include <iostream>

namespace proxy {

AppLogger::AppLogger(
    std::string loggerNameValue,
    LogLevel levelValue,
    std::optional<std::string> filePathValue,
    bool consoleEnabledValue)
    : loggerName(std::move(loggerNameValue)),
      level(levelValue),
      filePath(std::move(filePathValue)),
      consoleEnabled(consoleEnabledValue) {
    if (filePath.has_value()) {
        fileStream = std::make_unique<std::ofstream>(*filePath, std::ios::app);
    }
}

AppLogger::~AppLogger() = default;

void AppLogger::debug(const std::string& message) { log(LogLevel::DEBUG, message); }

void AppLogger::info(const std::string& message) { log(LogLevel::INFO, message); }

void AppLogger::warn(const std::string& message) { log(LogLevel::WARN, message); }

void AppLogger::error(const std::string& message) { log(LogLevel::ERROR, message); }

void AppLogger::log(LogLevel messageLevel, const std::string& message) {
    if (!shouldLog(messageLevel)) {
        return;
    }

    const std::string line =
        "[" + toIso8601(nowUtc()) + "]" + "[" + loggerName + "]" + "[" + toString(messageLevel) + "] " + message;

    std::lock_guard<std::mutex> lock(mutex);
    if (consoleEnabled) {
        std::cout << line << '\n';
    }
    if (fileStream && fileStream->is_open()) {
        (*fileStream) << line << '\n';
        fileStream->flush();
    }
}

bool AppLogger::shouldLog(LogLevel messageLevel) const {
    const auto priority = [](LogLevel value) {
        switch (value) {
            case LogLevel::DEBUG:
                return 0;
            case LogLevel::INFO:
                return 1;
            case LogLevel::WARN:
                return 2;
            case LogLevel::ERROR:
                return 3;
            default:
                return 4;
        }
    };
    return priority(messageLevel) >= priority(level);
}

}  // namespace proxy
