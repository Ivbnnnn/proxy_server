#pragma once

#include "proxy/CacheEntry.h"
#include "proxy/AppLogger.h"
#include "proxy/RedisService.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace proxy {

struct CacheSettings {
    int defaultTtlSeconds = 120;
    std::size_t maxEntries = 1000;              // 0 = без лимита
    std::size_t maxBytes = 0;                   // 0 = без лимита
};

struct CacheStats {
    std::size_t entries = 0;
    std::size_t bytes = 0;
    std::size_t overflowEvents = 0;
};

class CacheService {
public:
    CacheService(RedisService& redisValue, AppLogger& loggerValue, CacheSettings settingsValue);

    int getDefaultTtlSeconds() const;
    std::size_t getMaxEntries() const;
    std::size_t getMaxBytes() const;
    void setDefaultTtlSeconds(int ttlSeconds);

    void setLimits(std::size_t maxEntries, std::size_t maxBytes);
    CacheStats getStats();

    std::optional<CacheEntry> get(const std::string& key);
    void put(const CacheEntry& entry);

    bool removeByKey(const std::string& key);
    std::size_t removeByUrl(const std::string& url);
    void clear();

private:
    static constexpr const char* kCachePrefix = "proxy:cache:item:";

    std::string makeRedisKey(const std::string& cacheKey) const;
    std::string serialize(const CacheEntry& entry) const;
    std::optional<CacheEntry> deserialize(const std::string& raw) const;

    std::size_t estimateSize(const CacheEntry& entry) const;
    void rebuildStateFromRedis();
    void enforceLimits();
    void pruneIfNeeded(std::size_t incomingBytes);
    void evictOldest(std::size_t count);
    void eraseFromOrder(const std::string& cacheKey);
    void noteOverflow(std::size_t evictedCount);

    static std::int64_t toUnixMs(const DateTime& dt);
    static DateTime fromUnixMs(std::int64_t ms);

private:
    RedisService& redis;
    AppLogger& logger;
    CacheSettings settings;

    mutable CacheStats stats;
    std::deque<std::string> order;
};

}  // namespace proxy
