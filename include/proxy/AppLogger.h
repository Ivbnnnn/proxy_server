#pragma once

#include "proxy/Types.h"

#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace proxy {

class AppLogger {
public:
    AppLogger(std::string loggerName, LogLevel level, std::optional<std::string> filePath, bool consoleEnabled);
    ~AppLogger();

    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    void log(LogLevel messageLevel, const std::string& message);
    [[nodiscard]] bool shouldLog(LogLevel messageLevel) const;

    std::string loggerName;
    LogLevel level{LogLevel::INFO};
    std::optional<std::string> filePath;
    bool consoleEnabled{true};
    std::unique_ptr<std::ofstream> fileStream;
    mutable std::mutex mutex;
};

}  // namespace proxy
