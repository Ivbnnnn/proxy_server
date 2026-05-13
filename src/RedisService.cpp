#include "proxy/RedisService.h"
#include <iterator>
#include <stdexcept>

namespace proxy {

RedisService::RedisService(
    std::string hostValue,
    int portValue,
    int dbIndexValue,
    std::optional<std::string> passwordValue,
    int timeoutMsValue,
    AppLogger& loggerValue)
    : host(std::move(hostValue)),
      port(portValue),
      dbIndex(dbIndexValue),
      password(std::move(passwordValue)),
      timeoutMs(timeoutMsValue),
      logger(loggerValue) {
    sw::redis::ConnectionOptions options;
    options.host = host;
    options.port = port;
    options.db = dbIndex;
    options.socket_timeout = std::chrono::milliseconds(timeoutMs);
    if (password.has_value()) {
        options.password = *password;
    }
    redis = std::make_unique<sw::redis::Redis>(options);
    redis->ping();
    if (!redis) {
        throw std::runtime_error("Failed to create Redis client");
    }
    logger.info("RedisService connected to Redis: " + host + ":" + std::to_string(port));
    logger.info("RedisService initialized at " + host + ":" + std::to_string(port));
}

std::optional<std::string> RedisService::get(const std::string& key) {
    auto value = redis->get(key);
    if (!value) {
        return std::nullopt;
    }
    return *value;
}

void RedisService::set(const std::string& key, const std::string& value, int ttlSeconds) {
    if (ttlSeconds > 0) {
        redis->setex(key, ttlSeconds, value);
    } else {
        redis->set(key, value);
    }
}

std::vector<std::string> RedisService::keys(const std::string& pattern) {
    std::vector<std::string> out;
    redis->keys(pattern, std::back_inserter(out));
    return out;
}

void RedisService::del(const std::string& key) { redis->del(key); }

bool RedisService::exists(const std::string& key) { return redis->exists(key) > 0; }

void RedisService::expire(const std::string& key, int ttlSeconds) { redis->expire(key, ttlSeconds); }

}  // namespace proxy
