#include "TestSupport.h"

#include "proxy/AdminUI.h"
#include "proxy/ProxyController.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

using namespace proxy;
using namespace std::chrono_literals;

namespace {

AppLogger silentLogger() {
    return AppLogger("proxy-tests", LogLevel::ERROR, std::nullopt, false);
}

CacheEntry cacheEntry(
    const std::string& key,
    const std::string& url = "http://example.test/resource",
    const std::string& body = "body",
    int ttlSeconds = 120) {
    return CacheEntry(
        key,
        url,
        "GET",
        200,
        body,
        "Content-Type:text/plain",
        ttlSeconds);
}

}  // namespace

TEST(TypesTest, ConvertsStringsAndEnums) {
    EXPECT_EQ(toLower("AbC-123_"), "abc-123_");
    EXPECT_EQ(toString(LogLevel::DEBUG), "DEBUG");
    EXPECT_EQ(toString(LogLevel::INFO), "INFO");
    EXPECT_EQ(toString(LogLevel::WARN), "WARN");
    EXPECT_EQ(toString(LogLevel::ERROR), "ERROR");
    EXPECT_EQ(toString(static_cast<LogLevel>(99)), "UNKNOWN");
    EXPECT_EQ(toString(FilterDecision::ALLOW), "ALLOW");
    EXPECT_EQ(toString(FilterDecision::BLOCK), "BLOCK");
    EXPECT_EQ(toString(static_cast<FilterDecision>(99)), "UNKNOWN");
    EXPECT_EQ(toString(MatchType::EXACT), "EXACT");
    EXPECT_EQ(toString(MatchType::PREFIX), "PREFIX");
    EXPECT_EQ(toString(MatchType::CONTAINS), "CONTAINS");
    EXPECT_EQ(toString(MatchType::REGEX), "REGEX");
    EXPECT_EQ(toString(static_cast<MatchType>(99)), "UNKNOWN");
}

TEST(TypesTest, ConvertsTimeRoundTripAndIso8601) {
    const auto value = fromUnixSeconds(1712345678);
    EXPECT_EQ(toUnixSeconds(value), 1712345678);

    const std::string iso = toIso8601(value);
    EXPECT_EQ(iso.size(), 20U);
    EXPECT_EQ(iso[4], '-');
    EXPECT_EQ(iso[10], 'T');
    EXPECT_EQ(iso[19], 'Z');
}

TEST(AdminUserTest, StoresConstructorValuesAndDefaults) {
    AdminUser user(7, "admin", "hash", true);

    EXPECT_EQ(user.getId(), 7);
    EXPECT_EQ(user.getLogin(), "admin");
    EXPECT_EQ(user.getPasswordHash(), "hash");
    EXPECT_EQ(user.getRole(), "ADMIN");
    EXPECT_TRUE(user.getIsActive());
    EXPECT_FALSE(user.getLastLoginAt().has_value());
    EXPECT_TRUE(user.canLogin());
}

TEST(AdminUserTest, CanLoginRequiresActiveAdminWithLogin) {
    AdminUser user(1, "admin", "hash", true);
    EXPECT_TRUE(user.canLogin());

    user.setIsActive(false);
    EXPECT_FALSE(user.canLogin());

    user.setIsActive(true);
    user.setRole("VIEWER");
    EXPECT_FALSE(user.canLogin());

    user.setRole("ADMIN");
    user.setLogin("");
    EXPECT_FALSE(user.canLogin());
}

TEST(AdminUserTest, SettersUpdateAllMutableFields) {
    AdminUser user;
    const auto createdAt = fromUnixSeconds(1000);
    const auto lastLoginAt = fromUnixSeconds(2000);

    user.setId(11);
    user.setLogin("root");
    user.setPasswordHash("sha256:value");
    user.setRole("ADMIN");
    user.setIsActive(true);
    user.setCreatedAt(createdAt);
    user.setLastLoginAt(lastLoginAt);

    EXPECT_EQ(user.getId(), 11);
    EXPECT_EQ(user.getLogin(), "root");
    EXPECT_EQ(user.getPasswordHash(), "sha256:value");
    EXPECT_EQ(user.getRole(), "ADMIN");
    EXPECT_EQ(toUnixSeconds(user.getCreatedAt()), 1000);
    ASSERT_TRUE(user.getLastLoginAt().has_value());
    EXPECT_EQ(toUnixSeconds(*user.getLastLoginAt()), 2000);
}

TEST(WhitelistRuleTest, MatchesExactPrefixContainsRegexCaseInsensitive) {
    EXPECT_TRUE(WhitelistRule(1, "HTTP://EXAMPLE.COM/A", MatchType::EXACT, true, "").matches("http://example.com/a"));
    EXPECT_FALSE(WhitelistRule(1, "http://example.com/a", MatchType::EXACT, true, "").matches("http://example.com/a/b"));

    EXPECT_TRUE(WhitelistRule(2, "http://example.com/api", MatchType::PREFIX, true, "").matches("http://example.com/api/users"));
    EXPECT_FALSE(WhitelistRule(2, "http://example.com/api", MatchType::PREFIX, true, "").matches("http://example.com/static"));

    EXPECT_TRUE(WhitelistRule(3, "allowed", MatchType::CONTAINS, true, "").matches("http://site.test/Allowed/path"));
    EXPECT_FALSE(WhitelistRule(3, "allowed", MatchType::CONTAINS, true, "").matches("http://site.test/blocked"));

    EXPECT_TRUE(WhitelistRule(4, R"(example\.(com|org)/\d+)", MatchType::REGEX, true, "").matches("https://example.org/42"));
    EXPECT_FALSE(WhitelistRule(4, R"(example\.(com|org)/\d+)", MatchType::REGEX, true, "").matches("https://example.net/42"));
}

TEST(WhitelistRuleTest, RejectsDisabledEmptyAndInvalidRegexRules) {
    EXPECT_FALSE(WhitelistRule(1, "http://example.com", MatchType::EXACT, false, "").matches("http://example.com"));
    EXPECT_FALSE(WhitelistRule(1, "", MatchType::CONTAINS, true, "").matches("http://example.com"));
    EXPECT_FALSE(WhitelistRule(1, "[", MatchType::REGEX, true, "").matches("http://example.com"));
}

TEST(WhitelistRuleTest, SettersUpdateMetadata) {
    WhitelistRule rule;
    rule.setId(5);
    rule.setUrlPattern("example");
    rule.setMatchType(MatchType::CONTAINS);
    rule.setEnabled(false);
    rule.setComment("comment");
    rule.setCreatedAt(fromUnixSeconds(10));
    rule.setUpdatedAt(fromUnixSeconds(20));

    EXPECT_EQ(rule.getId(), 5);
    EXPECT_EQ(rule.getUrlPattern(), "example");
    EXPECT_EQ(rule.getMatchType(), MatchType::CONTAINS);
    EXPECT_FALSE(rule.getEnabled());
    EXPECT_EQ(rule.getComment(), "comment");
    EXPECT_EQ(toUnixSeconds(rule.getCreatedAt()), 10);
    EXPECT_EQ(toUnixSeconds(rule.getUpdatedAt()), 20);
}

TEST(CacheEntryTest, ConstructorSetsResponseAndExpirationData) {
    CacheEntry entry("key", "http://x", "POST", 201, "payload", "H:V", 30);

    EXPECT_EQ(entry.getCacheKey(), "key");
    EXPECT_EQ(entry.getUrl(), "http://x");
    EXPECT_EQ(entry.getMethod(), "POST");
    EXPECT_EQ(entry.getStatusCode(), 201);
    EXPECT_EQ(entry.getBody(), "payload");
    EXPECT_EQ(entry.getHeaders(), "H:V");
    EXPECT_EQ(entry.getTtlSeconds(), 30);
    EXPECT_FALSE(entry.getLastAccessAt().has_value());
    EXPECT_EQ(entry.getHitCount(), 0);
    EXPECT_FALSE(entry.isExpired(nowUtc()));
}

TEST(CacheEntryTest, TouchUpdatesAccessTimeAndHitCount) {
    CacheEntry entry = cacheEntry("key");
    entry.touch();
    EXPECT_TRUE(entry.getLastAccessAt().has_value());
    EXPECT_EQ(entry.getHitCount(), 1);

    entry.touch();
    EXPECT_EQ(entry.getHitCount(), 2);
}

TEST(CacheEntryTest, SettersAndExpirationChecksWork) {
    CacheEntry entry;
    entry.setCacheKey("k");
    entry.setUrl("u");
    entry.setMethod("PUT");
    entry.setStatusCode(202);
    entry.setBody("b");
    entry.setHeaders("h");
    entry.setTtlSeconds(5);
    entry.setCreatedAt(fromUnixSeconds(50));
    entry.setExpiresAt(fromUnixSeconds(55));
    entry.setLastAccessAt(fromUnixSeconds(54));
    entry.setHitCount(9);

    EXPECT_EQ(entry.getCacheKey(), "k");
    EXPECT_EQ(entry.getUrl(), "u");
    EXPECT_EQ(entry.getMethod(), "PUT");
    EXPECT_EQ(entry.getStatusCode(), 202);
    EXPECT_EQ(entry.getBody(), "b");
    EXPECT_EQ(entry.getHeaders(), "h");
    EXPECT_EQ(entry.getTtlSeconds(), 5);
    EXPECT_EQ(toUnixSeconds(entry.getCreatedAt()), 50);
    EXPECT_EQ(toUnixSeconds(entry.getExpiresAt()), 55);
    ASSERT_TRUE(entry.getLastAccessAt().has_value());
    EXPECT_EQ(toUnixSeconds(*entry.getLastAccessAt()), 54);
    EXPECT_EQ(entry.getHitCount(), 9);
    EXPECT_FALSE(entry.isExpired(fromUnixSeconds(54)));
    EXPECT_TRUE(entry.isExpired(fromUnixSeconds(55)));
}

TEST(RequestRecordTest, DefaultsAndSettersWork) {
    RequestRecord record;
    EXPECT_EQ(record.getId(), 0);
    EXPECT_EQ(record.getStatusCode(), 0);
    EXPECT_FALSE(record.getAllowed());
    EXPECT_FALSE(record.getCacheHit());
    EXPECT_FALSE(record.getMatchedRuleId().has_value());

    record.setId(9);
    record.setUrl("http://x");
    record.setMethod("PATCH");
    record.setClientIp("127.0.0.1");
    record.setStatusCode(204);
    record.setAllowed(true);
    record.setCacheHit(true);
    record.setMatchedRuleId(3);
    record.setResponseTimeMs(44);
    record.setRequestedAt(fromUnixSeconds(123));

    EXPECT_EQ(record.getId(), 9);
    EXPECT_EQ(record.getUrl(), "http://x");
    EXPECT_EQ(record.getMethod(), "PATCH");
    EXPECT_EQ(record.getClientIp(), "127.0.0.1");
    EXPECT_EQ(record.getStatusCode(), 204);
    EXPECT_TRUE(record.getAllowed());
    EXPECT_TRUE(record.getCacheHit());
    ASSERT_TRUE(record.getMatchedRuleId().has_value());
    EXPECT_EQ(*record.getMatchedRuleId(), 3);
    EXPECT_EQ(record.getResponseTimeMs(), 44);
    EXPECT_EQ(toUnixSeconds(record.getRequestedAt()), 123);

    record.setMatchedRuleId(std::nullopt);
    EXPECT_FALSE(record.getMatchedRuleId().has_value());
}

TEST(AppLoggerTest, FiltersByLevelAndWritesToFile) {
    const auto path = std::filesystem::temp_directory_path() / "proxy_logger_test.log";
    std::filesystem::remove(path);

    {
        AppLogger logger("logger-test", LogLevel::WARN, path.string(), false);
        logger.debug("debug-hidden");
        logger.info("info-hidden");
        logger.warn("warn-visible");
        logger.error("");
    }

    std::ifstream in(path);
    ASSERT_TRUE(in.is_open());
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    EXPECT_EQ(content.find("debug-hidden"), std::string::npos);
    EXPECT_EQ(content.find("info-hidden"), std::string::npos);
    EXPECT_NE(content.find("warn-visible"), std::string::npos);
    EXPECT_NE(content.find("[ERROR]"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(RedisServiceIntegrationTest, SupportsBasicKeyOperations) {
    auto logger = silentLogger();
    auto redis = test::createCleanRedis(logger);
    redis->del("proxy:test:redis:a");

    EXPECT_FALSE(redis->get("proxy:test:redis:a").has_value());
    EXPECT_FALSE(redis->exists("proxy:test:redis:a"));

    redis->set("proxy:test:redis:a", "value", 30);
    ASSERT_TRUE(redis->get("proxy:test:redis:a").has_value());
    EXPECT_EQ(*redis->get("proxy:test:redis:a"), "value");
    EXPECT_TRUE(redis->exists("proxy:test:redis:a"));

    redis->expire("proxy:test:redis:a", 30);
    const auto keys = redis->keys("proxy:test:redis:*");
    EXPECT_FALSE(keys.empty());

    redis->del("proxy:test:redis:a");
    EXPECT_FALSE(redis->exists("proxy:test:redis:a"));
}

TEST(RedisServiceIntegrationTest, ExpiresKeysByTtl) {
    auto logger = silentLogger();
    auto redis = test::createCleanRedis(logger);
    redis->set("proxy:test:redis:ttl", "short", 1);
    std::this_thread::sleep_for(1200ms);
    EXPECT_FALSE(redis->get("proxy:test:redis:ttl").has_value());
}

TEST(DBServiceIntegrationTest, PersistsWhitelistRules) {
    auto logger = silentLogger();
    auto db = test::createCleanDb(logger);

    db->saveWhitelistRule(test::whitelistRule("http://example.com", MatchType::EXACT, true, "created"));
    auto rules = db->loadWhitelistRules();
    ASSERT_EQ(rules.size(), 1U);
    EXPECT_EQ(rules[0].getId(), 1);
    EXPECT_EQ(rules[0].getUrlPattern(), "http://example.com");
    EXPECT_EQ(rules[0].getMatchType(), MatchType::EXACT);
    EXPECT_TRUE(rules[0].getEnabled());
    EXPECT_EQ(rules[0].getComment(), "created");

    rules[0].setUrlPattern("http://updated.example");
    rules[0].setMatchType(MatchType::PREFIX);
    rules[0].setEnabled(false);
    rules[0].setComment("updated");
    db->saveWhitelistRule(rules[0]);

    rules = db->loadWhitelistRules();
    ASSERT_EQ(rules.size(), 1U);
    EXPECT_EQ(rules[0].getUrlPattern(), "http://updated.example");
    EXPECT_EQ(rules[0].getMatchType(), MatchType::PREFIX);
    EXPECT_FALSE(rules[0].getEnabled());
    EXPECT_EQ(rules[0].getComment(), "updated");

    db->deleteWhitelistRule(rules[0].getId());
    EXPECT_TRUE(db->loadWhitelistRules().empty());
}

TEST(DBServiceIntegrationTest, PersistsRequestsAndAdminUsers) {
    auto logger = silentLogger();
    auto db = test::createCleanDb(logger);

    AdminUser admin(0, "admin", "hash1", true);
    db->saveAdminUser(admin);

    auto loadedAdmin = db->getAdminUser("admin");
    ASSERT_TRUE(loadedAdmin.has_value());
    EXPECT_EQ(loadedAdmin->getLogin(), "admin");
    EXPECT_EQ(loadedAdmin->getPasswordHash(), "hash1");
    EXPECT_FALSE(loadedAdmin->getLastLoginAt().has_value());

    admin.setPasswordHash("hash2");
    admin.setLastLoginAt(fromUnixSeconds(200));
    db->saveAdminUser(admin);
    loadedAdmin = db->getAdminUser("admin");
    ASSERT_TRUE(loadedAdmin.has_value());
    EXPECT_EQ(loadedAdmin->getPasswordHash(), "hash2");
    ASSERT_TRUE(loadedAdmin->getLastLoginAt().has_value());
    EXPECT_EQ(toUnixSeconds(*loadedAdmin->getLastLoginAt()), 200);
    EXPECT_EQ(db->loadAdminUsers().size(), 1U);

    RequestRecord record;
    record.setUrl("http://example.com/a");
    record.setMethod("GET");
    record.setClientIp("10.0.0.1");
    record.setStatusCode(200);
    record.setAllowed(true);
    record.setCacheHit(false);
    record.setMatchedRuleId(7);
    record.setResponseTimeMs(12);
    record.setRequestedAt(fromUnixSeconds(300));
    db->saveRequest(record);

    const auto requests = db->loadRequests();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests[0].getUrl(), "http://example.com/a");
    EXPECT_EQ(requests[0].getMethod(), "GET");
    EXPECT_EQ(requests[0].getClientIp(), "10.0.0.1");
    EXPECT_EQ(requests[0].getStatusCode(), 200);
    EXPECT_TRUE(requests[0].getAllowed());
    EXPECT_FALSE(requests[0].getCacheHit());
    ASSERT_TRUE(requests[0].getMatchedRuleId().has_value());
    EXPECT_EQ(*requests[0].getMatchedRuleId(), 7);
    EXPECT_EQ(requests[0].getResponseTimeMs(), 12);
}

TEST(CacheServiceIntegrationTest, PutsGetsTouchesAndRemovesEntries) {
    auto logger = silentLogger();
    auto redis = test::createCleanRedis(logger);
    CacheService cache(*redis, logger, test::testCacheSettings());

    EXPECT_FALSE(cache.get("missing").has_value());

    cache.put(cacheEntry("a", "http://example.test/a", "body-a"));
    auto loaded = cache.get("a");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->getBody(), "body-a");
    EXPECT_EQ(loaded->getHitCount(), 1);
    EXPECT_TRUE(loaded->getLastAccessAt().has_value());

    auto stats = cache.getStats();
    EXPECT_EQ(stats.entries, 1U);
    EXPECT_GT(stats.bytes, 0U);

    EXPECT_EQ(cache.removeByUrl("http://example.test/a"), 1U);
    EXPECT_FALSE(cache.get("a").has_value());
}

TEST(CacheServiceIntegrationTest, HandlesExpiredEntriesLimitsAndClear) {
    auto logger = silentLogger();
    auto redis = test::createCleanRedis(logger);
    CacheService cache(*redis, logger, test::testCacheSettings(120, 1, 0));

    auto expired = cacheEntry("expired");
    expired.setExpiresAt(nowUtc() - 1s);
    cache.put(expired);
    EXPECT_FALSE(cache.get("expired").has_value());

    cache.put(cacheEntry("old", "http://example.test/old", "old"));
    cache.put(cacheEntry("new", "http://example.test/new", "new"));
    EXPECT_FALSE(cache.get("old").has_value());
    ASSERT_TRUE(cache.get("new").has_value());

    cache.setDefaultTtlSeconds(33);
    EXPECT_EQ(cache.getDefaultTtlSeconds(), 33);
    EXPECT_THROW(cache.setDefaultTtlSeconds(0), std::invalid_argument);

    cache.setLimits(10, 10);
    cache.put(cacheEntry("large", "http://example.test/large", "large-body-over-limit"));
    EXPECT_LE(cache.getStats().entries, 1U);

    cache.clear();
    EXPECT_EQ(cache.getStats().entries, 0U);
}

TEST(CacheServiceIntegrationTest, EnforcesLoweredByteLimitOnExistingEntries) {
    auto logger = silentLogger();
    auto redis = test::createCleanRedis(logger);
    CacheService cache(*redis, logger, test::testCacheSettings(120, 10, 0));

    cache.put(cacheEntry("large", "http://example.test/large", "large-body-over-limit"));
    ASSERT_GT(cache.getStats().bytes, 10U);

    cache.setLimits(10, 10);

    const auto stats = cache.getStats();
    EXPECT_LE(stats.bytes, 10U);
    EXPECT_EQ(stats.entries, 0U);
    EXPECT_FALSE(cache.get("large").has_value());
}

TEST(FilterServiceIntegrationTest, AllowsOnlyEnabledMatchingRulesAndReloads) {
    auto logger = silentLogger();
    auto db = test::createCleanDb(logger);
    db->saveWhitelistRule(test::whitelistRule("blocked", MatchType::CONTAINS, false));
    db->saveWhitelistRule(test::whitelistRule("example.com/api", MatchType::CONTAINS, true));

    FilterService filter(*db, logger);
    EXPECT_FALSE(filter.isAllowed("http://site.test/blocked"));
    EXPECT_TRUE(filter.isAllowed("https://example.com/api/users"));
    EXPECT_EQ(filter.check("https://other.test"), FilterDecision::BLOCK);

    auto match = filter.findMatchingRule("https://example.com/api/users");
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->getId(), 2);

    db->saveWhitelistRule(test::whitelistRule("other.test", MatchType::CONTAINS, true));
    EXPECT_FALSE(filter.isAllowed("https://other.test"));
    filter.reloadRules();
    EXPECT_TRUE(filter.isAllowed("https://other.test"));
}

TEST(ProxyControllerIntegrationTest, BlocksRequestsWithoutWhitelistAndPersistsHistory) {
    auto logger = silentLogger();
    auto db = test::createCleanDb(logger);
    auto redis = test::createCleanRedis(logger);
    CacheService cache(*redis, logger, test::testCacheSettings());
    FilterService filter(*db, logger);
    ProxyController controller(filter, cache, *db, logger);

    std::optional<ProxyController::ProxyResponse> result;
    controller.handleRequestAsync(
        "http://blocked.test",
        "GET",
        "10.0.0.2",
        [](const std::string&, const std::string&, ProxyController::AsyncFetchCallback callback) {
            callback(ProxyController::UpstreamResponse{200, "should-not-run", "Content-Type:text/plain"});
        },
        [&](ProxyController::ProxyResponse response) { result = std::move(response); });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->decision, FilterDecision::BLOCK);
    EXPECT_EQ(result->statusCode, 403);
    EXPECT_FALSE(result->cacheHit);

    const auto requests = db->loadRequests();
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_FALSE(requests[0].getAllowed());
    EXPECT_EQ(requests[0].getStatusCode(), 403);
}

TEST(ProxyControllerIntegrationTest, CachesAllowedUpstreamResponses) {
    auto logger = silentLogger();
    auto db = test::createCleanDb(logger);
    db->saveWhitelistRule(test::whitelistRule("http://allowed.test", MatchType::PREFIX, true));
    auto redis = test::createCleanRedis(logger);
    CacheService cache(*redis, logger, test::testCacheSettings());
    FilterService filter(*db, logger);
    ProxyController controller(filter, cache, *db, logger);

    int fetchCount = 0;
    auto fetcher = [&](const std::string& url, const std::string& method, ProxyController::AsyncFetchCallback callback) {
        ++fetchCount;
        callback(ProxyController::UpstreamResponse{201, "upstream:" + method + ":" + url, "Content-Type:text/plain"});
    };

    std::optional<ProxyController::ProxyResponse> first;
    controller.handleRequestAsync("http://allowed.test/a", "GET", "10.0.0.3", fetcher, [&](auto response) {
        first = std::move(response);
    });
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->decision, FilterDecision::ALLOW);
    EXPECT_EQ(first->statusCode, 201);
    EXPECT_FALSE(first->cacheHit);
    EXPECT_EQ(fetchCount, 1);

    std::optional<ProxyController::ProxyResponse> second;
    controller.handleRequestAsync("http://allowed.test/a", "GET", "10.0.0.3", fetcher, [&](auto response) {
        second = std::move(response);
    });
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->statusCode, 201);
    EXPECT_TRUE(second->cacheHit);
    EXPECT_EQ(second->body, first->body);
    EXPECT_EQ(fetchCount, 1);
    EXPECT_EQ(controller.makeCacheKey("http://allowed.test/a", "GET"), "get:http://allowed.test/a");
    ASSERT_TRUE(controller.getCachedResponse("http://allowed.test/a", "GET").has_value());
    EXPECT_EQ(db->loadRequests().size(), 2U);
}

TEST(ProxyControllerIntegrationTest, ReportsFetcherFailures) {
    auto logger = silentLogger();
    auto db = test::createCleanDb(logger);
    db->saveWhitelistRule(test::whitelistRule("http://allowed.test", MatchType::PREFIX, true));
    auto redis = test::createCleanRedis(logger);
    CacheService cache(*redis, logger, test::testCacheSettings());
    FilterService filter(*db, logger);
    ProxyController controller(filter, cache, *db, logger);

    std::optional<ProxyController::ProxyResponse> failed;
    controller.handleRequestAsync(
        "http://allowed.test/fail",
        "GET",
        "10.0.0.4",
        [](const std::string&, const std::string&, ProxyController::AsyncFetchCallback callback) {
            callback(std::nullopt);
        },
        [&](auto response) { failed = std::move(response); });
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->statusCode, 502);
    EXPECT_FALSE(failed->cacheHit);

    std::optional<ProxyController::ProxyResponse> missingFetcher;
    controller.handleRequestAsync(
        "http://allowed.test/missing-fetcher",
        "GET",
        "10.0.0.4",
        ProxyController::AsyncFetcher{},
        [&](auto response) { missingFetcher = std::move(response); });
    ASSERT_TRUE(missingFetcher.has_value());
    EXPECT_EQ(missingFetcher->statusCode, 500);
}

TEST(AdminUIIntegrationTest, ManagesDataAndExportsCsv) {
    auto logger = silentLogger();
    auto db = test::createCleanDb(logger);
    AdminUI admin(*db, logger);

    admin.createWhitelistRule(test::whitelistRule("http://admin.test", MatchType::PREFIX, true, "one"));
    auto rules = admin.listWhitelistRules();
    ASSERT_EQ(rules.size(), 1U);

    rules[0].setComment("two");
    admin.updateWhitelistRule(rules[0]);
    rules = admin.listWhitelistRules();
    ASSERT_EQ(rules.size(), 1U);
    EXPECT_EQ(rules[0].getComment(), "two");

    AdminUser user(0, "operator", "hash", true);
    db->saveAdminUser(user);
    EXPECT_EQ(admin.listAdminUsers().size(), 1U);

    RequestRecord record;
    record.setUrl("http://admin.test/path,with,commas");
    record.setMethod("GET");
    record.setClientIp("10.0.0.1\"quoted");
    record.setStatusCode(200);
    record.setAllowed(true);
    record.setCacheHit(true);
    record.setResponseTimeMs(5);
    db->saveRequest(record);

    EXPECT_EQ(admin.listRequests().size(), 1U);

    const auto path = std::filesystem::temp_directory_path() / "proxy_requests_export_test.csv";
    std::filesystem::remove(path);
    admin.exportRequestsCsv(path.string());
    std::ifstream in(path);
    ASSERT_TRUE(in.is_open());
    const std::string csv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    EXPECT_NE(csv.find("id;url;method;clientIp;statusCode;allowed;cacheHit;matchedRuleId;responseTimeMs;requestedAt"), std::string::npos);
    EXPECT_NE(csv.find("http://admin.test/path,with,commas"), std::string::npos);
    EXPECT_NE(csv.find("\"10.0.0.1\"\"quoted\""), std::string::npos);

    admin.deleteWhitelistRule(rules[0].getId());
    EXPECT_TRUE(admin.listWhitelistRules().empty());
    std::filesystem::remove(path);
}
