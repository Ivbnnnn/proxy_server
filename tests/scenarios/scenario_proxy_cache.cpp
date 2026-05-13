#include "TestSupport.h"

#include "proxy/FilterService.h"
#include "proxy/ProxyController.h"

#include <exception>
#include <iostream>
#include <optional>

int main() {
    try {
        proxy::AppLogger logger("scenario-proxy-cache", proxy::LogLevel::ERROR, std::nullopt, false);
        auto db = proxy::test::createCleanDb(logger);
        db->saveWhitelistRule(proxy::test::whitelistRule("http://cache.scenario", proxy::MatchType::PREFIX, true));
        auto redis = proxy::test::createCleanRedis(logger);
        proxy::CacheService cache(*redis, logger, proxy::test::testCacheSettings());
        proxy::FilterService filter(*db, logger);
        proxy::ProxyController controller(filter, cache, *db, logger);

        int fetchCount = 0;
        auto fetcher = [&](const std::string& url, const std::string& method, proxy::ProxyController::AsyncFetchCallback callback) {
            ++fetchCount;
            callback(proxy::ProxyController::UpstreamResponse{200, "origin:" + method + ":" + url, "Content-Type:text/plain"});
        };

        std::optional<proxy::ProxyController::ProxyResponse> first;
        controller.handleRequestAsync("http://cache.scenario/item", "GET", "127.0.0.1", fetcher, [&](auto response) {
            first = std::move(response);
        });
        proxy::test::requireScenario(first.has_value(), "first proxy response is produced");
        proxy::test::requireScenario(first->decision == proxy::FilterDecision::ALLOW, "first proxy response is allowed");
        proxy::test::requireScenario(!first->cacheHit, "first proxy response is a cache miss");

        std::optional<proxy::ProxyController::ProxyResponse> second;
        controller.handleRequestAsync("http://cache.scenario/item", "GET", "127.0.0.1", fetcher, [&](auto response) {
            second = std::move(response);
        });
        proxy::test::requireScenario(second.has_value(), "second proxy response is produced");
        proxy::test::requireScenario(second->cacheHit, "second proxy response is served from cache");
        proxy::test::requireScenario(second->body == first->body, "cached body matches upstream body");
        proxy::test::requireScenario(fetchCount == 1, "upstream fetcher is called once");
        proxy::test::requireScenario(db->loadRequests().size() == 2, "both proxy calls are logged");

        std::cout << "scenario_proxy_cache passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
