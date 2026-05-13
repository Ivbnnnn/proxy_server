#include "proxy/CacheService.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>

#include <json/json.h>

namespace proxy {

namespace {
std::string jsonWrite(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

std::optional<Json::Value> jsonRead(const std::string& raw) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    Json::Value root;
    std::string errs;

    std::istringstream iss(raw);
    if (!Json::parseFromStream(builder, iss, &root, &errs)) {
        return std::nullopt;
    }
    return root;
}

template <class T>
T clampMin(T value, T minValue) {
    return value < minValue ? minValue : value;
}
}  // namespace

CacheService::CacheService(RedisService& redisValue, AppLogger& loggerValue, CacheSettings settingsValue)
    : redis(redisValue), logger(loggerValue), settings(settingsValue) {
    if (settings.defaultTtlSeconds <= 0) {
        settings.defaultTtlSeconds = 120;
    }
    rebuildStateFromRedis();
}

int CacheService::getDefaultTtlSeconds() const {
    return settings.defaultTtlSeconds;
}

std::size_t CacheService::getMaxEntries() const {
    return settings.maxEntries;
}

std::size_t CacheService::getMaxBytes() const {
    return settings.maxBytes;
}

void CacheService::setDefaultTtlSeconds(int ttlSeconds) {
    if (ttlSeconds <= 0) {
        throw std::invalid_argument("default TTL must be > 0");
    }
    settings.defaultTtlSeconds = ttlSeconds;
}

void CacheService::setLimits(std::size_t maxEntries, std::size_t maxBytes) {
    settings.maxEntries = maxEntries;
    settings.maxBytes = maxBytes;
    rebuildStateFromRedis();
    enforceLimits();
}

CacheStats CacheService::getStats() {
    // Always refresh from Redis so admin stats reflect TTL-expired removals.
    rebuildStateFromRedis();
    return stats;
}

std::string CacheService::makeRedisKey(const std::string& cacheKey) const {
    return std::string(kCachePrefix) + cacheKey;
}

std::int64_t CacheService::toUnixMs(const DateTime& dt) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(dt.time_since_epoch()).count();
}

DateTime CacheService::fromUnixMs(std::int64_t ms) {
    return DateTime(std::chrono::milliseconds(ms));
}

std::string CacheService::serialize(const CacheEntry& entry) const {
    Json::Value v;
    v["cacheKey"] = entry.getCacheKey();
    v["url"] = entry.getUrl();
    v["method"] = entry.getMethod();
    v["statusCode"] = entry.getStatusCode();
    v["body"] = entry.getBody();
    v["headers"] = entry.getHeaders();
    v["ttlSeconds"] = entry.getTtlSeconds();
    v["createdAtMs"] = Json::Int64(toUnixMs(entry.getCreatedAt()));
    v["expiresAtMs"] = Json::Int64(toUnixMs(entry.getExpiresAt()));
    v["hitCount"] = entry.getHitCount();

    if (entry.getLastAccessAt().has_value()) {
        v["lastAccessAtMs"] = Json::Int64(toUnixMs(*entry.getLastAccessAt()));
    } else {
        v["lastAccessAtMs"] = Json::nullValue;
    }

    return jsonWrite(v);
}

std::optional<CacheEntry> CacheService::deserialize(const std::string& raw) const {
    const auto json = jsonRead(raw);
    if (!json.has_value()) {
        return std::nullopt;
    }

    const Json::Value& v = *json;

    if (!v.isMember("cacheKey") || !v.isMember("url") || !v.isMember("method") ||
        !v.isMember("statusCode") || !v.isMember("body") || !v.isMember("headers") ||
        !v.isMember("ttlSeconds") || !v.isMember("createdAtMs") || !v.isMember("expiresAtMs")) {
        return std::nullopt;
    }

    CacheEntry entry(
        v["cacheKey"].asString(),
        v["url"].asString(),
        v["method"].asString(),
        v["statusCode"].asInt(),
        v["body"].asString(),
        v["headers"].asString(),
        v["ttlSeconds"].asInt());

    entry.setCreatedAt(fromUnixMs(static_cast<std::int64_t>(v["createdAtMs"].asLargestInt())));
    entry.setExpiresAt(fromUnixMs(static_cast<std::int64_t>(v["expiresAtMs"].asLargestInt())));

    if (v.isMember("lastAccessAtMs") && !v["lastAccessAtMs"].isNull()) {
        entry.setLastAccessAt(fromUnixMs(static_cast<std::int64_t>(v["lastAccessAtMs"].asLargestInt())));
    } else {
        entry.setLastAccessAt(std::nullopt);
    }

    if (v.isMember("hitCount")) {
        entry.setHitCount(v["hitCount"].asInt());
    }

    return entry;
}

std::size_t CacheService::estimateSize(const CacheEntry& entry) const {
    return entry.getCacheKey().size() +
           entry.getUrl().size() +
           entry.getMethod().size() +
           entry.getBody().size() +
           entry.getHeaders().size() +
           sizeof(int) * 3;
}

void CacheService::eraseFromOrder(const std::string& cacheKey) {
    order.erase(std::remove(order.begin(), order.end(), cacheKey), order.end());
}

void CacheService::noteOverflow(std::size_t evictedCount) {
    if (evictedCount == 0) {
        return;
    }
    ++stats.overflowEvents;
    logger.warn("Cache overflow: evicted " + std::to_string(evictedCount) + " entries");
}

void CacheService::rebuildStateFromRedis() {
    stats = {};
    order.clear();

    const auto keys = redis.keys(std::string(kCachePrefix) + "*");

    struct Item {
        std::string cacheKey;
        std::int64_t createdAtMs;
        std::size_t bytes;
    };

    std::vector<Item> items;
    items.reserve(keys.size());

    for (const auto& redisKey : keys) {
        const auto raw = redis.get(redisKey);
        if (!raw.has_value()) {
            continue;
        }

        const auto entry = deserialize(*raw);
        if (!entry.has_value()) {
            continue;
        }

        if (entry->isExpired(nowUtc())) {
            redis.del(redisKey);
            continue;
        }

        items.push_back(Item{entry->getCacheKey(), toUnixMs(entry->getCreatedAt()), estimateSize(*entry)});
        stats.bytes += estimateSize(*entry);
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.createdAtMs < b.createdAtMs;
    });

    for (const auto& item : items) {
        order.push_back(item.cacheKey);
    }

    stats.entries = items.size();
}

void CacheService::pruneIfNeeded(std::size_t incomingBytes) {
    std::size_t evicted = 0;

    auto overEntries = [&]() -> bool {
        return settings.maxEntries > 0 && (stats.entries + 1 > settings.maxEntries);
    };
    auto overBytes = [&]() -> bool {
        return settings.maxBytes > 0 && (stats.bytes + incomingBytes > settings.maxBytes);
    };

    while (!order.empty() && (overEntries() || overBytes())) {
        const std::string oldest = order.front();
        order.pop_front();

        const std::string redisKey = makeRedisKey(oldest);
        const auto raw = redis.get(redisKey);
        if (raw.has_value()) {
            const auto entry = deserialize(*raw);
            if (entry.has_value()) {
                stats.bytes = (stats.bytes >= estimateSize(*entry)) ? (stats.bytes - estimateSize(*entry)) : 0;
                stats.entries = (stats.entries > 0) ? (stats.entries - 1) : 0;
            }
            redis.del(redisKey);
        }
        ++evicted;
    }

    noteOverflow(evicted);
}

void CacheService::enforceLimits() {
    std::size_t evicted = 0;

    auto overEntries = [&]() -> bool {
        return settings.maxEntries > 0 && stats.entries > settings.maxEntries;
    };
    auto overBytes = [&]() -> bool {
        return settings.maxBytes > 0 && stats.bytes > settings.maxBytes;
    };

    while (!order.empty() && (overEntries() || overBytes())) {
        const std::string oldest = order.front();
        order.pop_front();

        const std::string redisKey = makeRedisKey(oldest);
        const auto raw = redis.get(redisKey);
        if (raw.has_value()) {
            const auto entry = deserialize(*raw);
            if (entry.has_value()) {
                stats.bytes = (stats.bytes >= estimateSize(*entry)) ? (stats.bytes - estimateSize(*entry)) : 0;
                stats.entries = (stats.entries > 0) ? (stats.entries - 1) : 0;
            }
            redis.del(redisKey);
        }
        ++evicted;
    }

    noteOverflow(evicted);
}

void CacheService::evictOldest(std::size_t count) {
    std::size_t evicted = 0;
    while (count > 0 && !order.empty()) {
        const std::string oldest = order.front();
        order.pop_front();

        const std::string redisKey = makeRedisKey(oldest);
        const auto raw = redis.get(redisKey);
        if (raw.has_value()) {
            const auto entry = deserialize(*raw);
            if (entry.has_value()) {
                stats.bytes = (stats.bytes >= estimateSize(*entry)) ? (stats.bytes - estimateSize(*entry)) : 0;
                stats.entries = (stats.entries > 0) ? (stats.entries - 1) : 0;
            }
            redis.del(redisKey);
        }

        ++evicted;
        --count;
    }

    noteOverflow(evicted);
}

std::optional<CacheEntry> CacheService::get(const std::string& key) {
    const std::string redisKey = makeRedisKey(key);
    const auto raw = redis.get(redisKey);
    if (!raw.has_value()) {
        // Redis may already delete expired keys by TTL, so keep in-memory stats in sync.
        rebuildStateFromRedis();
        return std::nullopt;
    }

    auto entry = deserialize(*raw);
    if (!entry.has_value()) {
        redis.del(redisKey);
        eraseFromOrder(key);
        return std::nullopt;
    }

    if (entry->isExpired(nowUtc())) {
        redis.del(redisKey);
        eraseFromOrder(key);
        stats.entries = (stats.entries > 0) ? (stats.entries - 1) : 0;
        stats.bytes = (stats.bytes >= estimateSize(*entry)) ? (stats.bytes - estimateSize(*entry)) : 0;
        return std::nullopt;
    }

    entry->touch();

    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(entry->getExpiresAt() - nowUtc()).count();
    if (remaining > 0) {
        redis.set(redisKey, serialize(*entry), static_cast<int>(remaining));
    } else {
        redis.del(redisKey);
        eraseFromOrder(key);
        return std::nullopt;
    }

    return entry;
}

void CacheService::put(const CacheEntry& entry) {
    // Keep state aligned with Redis before applying limits and accounting.
    rebuildStateFromRedis();

    const std::string redisKey = makeRedisKey(entry.getCacheKey());
    const std::size_t incomingBytes = estimateSize(entry);

    if (redis.exists(redisKey)) {
        const auto oldRaw = redis.get(redisKey);
        if (oldRaw.has_value()) {
            const auto oldEntry = deserialize(*oldRaw);
            if (oldEntry.has_value()) {
                stats.bytes = (stats.bytes >= estimateSize(*oldEntry)) ? (stats.bytes - estimateSize(*oldEntry)) : 0;
                stats.entries = (stats.entries > 0) ? (stats.entries - 1) : 0;
            }
        }
        eraseFromOrder(entry.getCacheKey());
    }

    pruneIfNeeded(incomingBytes);

    redis.set(redisKey, serialize(entry), clampMin(entry.getTtlSeconds(), 1));
    order.push_back(entry.getCacheKey());

    ++stats.entries;
    stats.bytes += incomingBytes;
}

bool CacheService::removeByKey(const std::string& key) {
    const std::string redisKey = makeRedisKey(key);
    const auto raw = redis.get(redisKey);
    if (!raw.has_value()) {
        return false;
    }

    const auto entry = deserialize(*raw);
    if (entry.has_value()) {
        stats.bytes = (stats.bytes >= estimateSize(*entry)) ? (stats.bytes - estimateSize(*entry)) : 0;
        stats.entries = (stats.entries > 0) ? (stats.entries - 1) : 0;
    }

    redis.del(redisKey);
    eraseFromOrder(key);
    return true;
}

std::size_t CacheService::removeByUrl(const std::string& url) {
    std::size_t removed = 0;

    const auto keys = redis.keys(std::string(kCachePrefix) + "*");
    for (const auto& redisKey : keys) {
        const auto raw = redis.get(redisKey);
        if (!raw.has_value()) {
            continue;
        }

        const auto entry = deserialize(*raw);
        if (!entry.has_value()) {
            continue;
        }

        if (entry->getUrl() == url) {
            if (removeByKey(entry->getCacheKey())) {
                ++removed;
            }
        }
    }

    return removed;
}

void CacheService::clear() {
    const auto keys = redis.keys(std::string(kCachePrefix) + "*");
    for (const auto& redisKey : keys) {
        redis.del(redisKey);
    }
    order.clear();
    stats.entries = 0;
    stats.bytes = 0;
}

}  // namespace proxy
