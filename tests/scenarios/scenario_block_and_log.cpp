#include "TestSupport.h"

#include "proxy/FilterService.h"
#include "proxy/ProxyController.h"

#include <exception>
#include <iostream>
#include <optional>

int main() {
    try {
        proxy::AppLogger logger("scenario-block-and-log", proxy::LogLevel::ERROR, std::nullopt, false);
        auto db = proxy::test::createCleanDb(logger);
        auto redis = proxy::test::createCleanRedis(logger);
        proxy::CacheService cache(*redis, logger, proxy::test::testCacheSettings());
        proxy::FilterService filter(*db, logger);
        proxy::ProxyController controller(filter, cache, *db, logger);

        std::optional<proxy::ProxyController::ProxyResponse> response;
        controller.handleRequestAsync(
            "http://blocked.scenario/item",
            "GET",
            "192.0.2.1",
            [](const std::string&, const std::string&, proxy::ProxyController::AsyncFetchCallback callback) {
                callback(proxy::ProxyController::UpstreamResponse{200, "unexpected", "Content-Type:text/plain"});
            },
            [&](auto result) { response = std::move(result); });

        proxy::test::requireScenario(response.has_value(), "blocked proxy response is produced");
        proxy::test::requireScenario(response->decision == proxy::FilterDecision::BLOCK, "request is blocked");
        proxy::test::requireScenario(response->statusCode == 403, "blocked request returns 403");

        const auto requests = db->loadRequests();
        proxy::test::requireScenario(requests.size() == 1, "blocked request is logged");
        proxy::test::requireScenario(!requests[0].getAllowed(), "logged blocked request is marked as not allowed");
        proxy::test::requireScenario(requests[0].getStatusCode() == 403, "logged blocked request keeps status code");

        std::cout << "scenario_block_and_log passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
