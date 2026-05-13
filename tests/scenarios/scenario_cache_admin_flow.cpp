#include "TestSupport.h"

#include <exception>
#include <iostream>
#include <optional>

int main() {
    try {
        proxy::AppLogger logger("scenario-cache-admin-flow", proxy::LogLevel::ERROR, std::nullopt, false);
        auto redis = proxy::test::createCleanRedis(logger);
        proxy::CacheService cache(*redis, logger, proxy::test::testCacheSettings(120, 10, 0));

        cache.put(proxy::CacheEntry("one", "http://cache-admin.scenario/one", "GET", 200, "one-body", "Content-Type:text/plain", 120));
        cache.put(proxy::CacheEntry("two", "http://cache-admin.scenario/two", "GET", 200, "two-body", "Content-Type:text/plain", 120));

        auto stats = cache.getStats();
        proxy::test::requireScenario(stats.entries == 2, "cache has two entries");
        proxy::test::requireScenario(stats.bytes > 0, "cache reports stored bytes");

        cache.setDefaultTtlSeconds(45);
        proxy::test::requireScenario(cache.getDefaultTtlSeconds() == 45, "cache TTL setting is updated");

        cache.setLimits(1, 0);
        stats = cache.getStats();
        proxy::test::requireScenario(stats.entries <= 1, "cache entry limit prunes old entries");

        cache.put(proxy::CacheEntry("remove", "http://cache-admin.scenario/remove", "GET", 200, "body", "Content-Type:text/plain", 120));
        const auto removed = cache.removeByUrl("http://cache-admin.scenario/remove");
        proxy::test::requireScenario(removed == 1, "cache removes entries by URL");

        std::cout << "scenario_cache_admin_flow passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
