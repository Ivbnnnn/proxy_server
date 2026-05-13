#pragma once

#include "proxy/AppLogger.h"
#include "proxy/CacheService.h"
#include "proxy/DBService.h"
#include "proxy/RedisService.h"
#include "proxy/WhitelistRule.h"

#include <memory>
#include <string>

namespace proxy::test {

std::string envOrDefault(const char* name, std::string defaultValue);
int envIntOrDefault(const char* name, int defaultValue);

std::string testDbConnectionString();
std::string testRedisHost();
int testRedisPort();
int testRedisDb();

std::unique_ptr<DBService> createCleanDb(AppLogger& logger);
std::unique_ptr<RedisService> createCleanRedis(AppLogger& logger);
CacheSettings testCacheSettings(int ttlSeconds = 120, std::size_t maxEntries = 1000, std::size_t maxBytes = 0);

void truncateDatabaseTables(const std::string& connectionString);
void clearProxyCacheKeys(RedisService& redis);

WhitelistRule whitelistRule(
    std::string pattern,
    MatchType matchType = MatchType::EXACT,
    bool enabled = true,
    std::string comment = "");

void requireScenario(bool condition, const std::string& message);

}  // namespace proxy::test
