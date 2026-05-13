#pragma once

#include "proxy/AppLogger.h"
#include "proxy/Types.h"

#include <mutex>
#include <memory>
#include <optional>
#include <string>

#include <sw/redis++/redis++.h>

namespace proxy {

class RedisService {
public:
    RedisService(
        std::string host,
        int port,
        int dbIndex,
        std::optional<std::string> password,
        int timeoutMs,
        AppLogger& logger);

    [[nodiscard]] std::optional<std::string> get(const std::string& key);
    void set(const std::string& key, const std::string& value, int ttlSeconds);
    void del(const std::string& key);
    [[nodiscard]] bool exists(const std::string& key);
    void expire(const std::string& key, int ttlSeconds);
    std::vector<std::string> keys(const std::string& pattern);

private:
    std::string host;
    int port{6379};
    int dbIndex{0};
    std::optional<std::string> password;
    int timeoutMs{1000};
    AppLogger& logger;
    

    std::unique_ptr<sw::redis::Redis> redis;
};

}  // namespace proxy
