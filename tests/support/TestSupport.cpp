#include "TestSupport.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <pqxx/pqxx>

namespace proxy::test {

namespace {

std::string databaseNameFromConnectionString(const std::string& connectionString) {
    constexpr std::string_view prefix = "dbname=";
    const auto pos = connectionString.find(prefix);
    if (pos == std::string::npos) {
        return "proxy_test_db";
    }

    const auto start = pos + prefix.size();
    const auto end = connectionString.find(' ', start);
    return connectionString.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string maintenanceConnectionString(const std::string& connectionString) {
    constexpr std::string_view prefix = "dbname=";
    const auto pos = connectionString.find(prefix);
    if (pos == std::string::npos) {
        return connectionString + " dbname=postgres";
    }

    const auto start = pos + prefix.size();
    const auto end = connectionString.find(' ', start);
    std::string out = connectionString;
    out.replace(start, end == std::string::npos ? std::string::npos : end - start, "postgres");
    return out;
}

void ensureTestDatabaseExists(const std::string& connectionString) {
    const std::string dbName = databaseNameFromConnectionString(connectionString);
    pqxx::connection connection(maintenanceConnectionString(connectionString));
    pqxx::nontransaction tx(connection);
    const auto exists = tx.exec("SELECT 1 FROM pg_database WHERE datname=" + tx.quote(dbName));
    if (exists.empty()) {
        tx.exec("CREATE DATABASE " + tx.quote_name(dbName));
    }
}

}  // namespace

std::string envOrDefault(const char* name, std::string defaultValue) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return defaultValue;
    }
    return value;
}

int envIntOrDefault(const char* name, int defaultValue) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return defaultValue;
    }
    return std::stoi(value);
}

std::string testDbConnectionString() {
    return envOrDefault(
        "PROXY_TEST_DB_CONN",
        "host=127.0.0.1 port=5433 dbname=proxy_test_db user=postgres password=postgres");
}

std::string testRedisHost() {
    return envOrDefault("PROXY_TEST_REDIS_HOST", "127.0.0.1");
}

int testRedisPort() {
    return envIntOrDefault("PROXY_TEST_REDIS_PORT", 6379);
}

int testRedisDb() {
    return envIntOrDefault("PROXY_TEST_REDIS_DB", 15);
}

std::unique_ptr<DBService> createCleanDb(AppLogger& logger) {
    const std::string connectionString = testDbConnectionString();
    std::unique_ptr<DBService> db;
    try {
        db = std::make_unique<DBService>(connectionString, 1, logger);
    } catch (const std::exception& ex) {
        if (std::string(ex.what()).find("does not exist") == std::string::npos) {
            throw;
        }
        ensureTestDatabaseExists(connectionString);
        db = std::make_unique<DBService>(connectionString, 1, logger);
    }
    truncateDatabaseTables(testDbConnectionString());
    return db;
}

std::unique_ptr<RedisService> createCleanRedis(AppLogger& logger) {
    auto redis = std::make_unique<RedisService>(
        testRedisHost(),
        testRedisPort(),
        testRedisDb(),
        std::nullopt,
        1000,
        logger);
    clearProxyCacheKeys(*redis);
    return redis;
}

CacheSettings testCacheSettings(int ttlSeconds, std::size_t maxEntries, std::size_t maxBytes) {
    CacheSettings settings;
    settings.defaultTtlSeconds = ttlSeconds;
    settings.maxEntries = maxEntries;
    settings.maxBytes = maxBytes;
    return settings;
}

void truncateDatabaseTables(const std::string& connectionString) {
    pqxx::connection connection(connectionString);
    pqxx::work tx(connection);
    tx.exec("TRUNCATE TABLE request_records, whitelist_rules, admin_users RESTART IDENTITY");
    tx.commit();
}

void clearProxyCacheKeys(RedisService& redis) {
    const auto keys = redis.keys("proxy:cache:item:*");
    for (const auto& key : keys) {
        redis.del(key);
    }
}

WhitelistRule whitelistRule(std::string pattern, MatchType matchType, bool enabled, std::string comment) {
    return WhitelistRule(0, std::move(pattern), matchType, enabled, std::move(comment));
}

void requireScenario(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Scenario failed: " << message << '\n';
        throw std::runtime_error(message);
    }
}

}  // namespace proxy::test
