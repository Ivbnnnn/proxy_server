#pragma once

#include "proxy/CacheService.h"
#include "proxy/DBService.h"
#include "proxy/FilterService.h"
#include "proxy/Types.h"

#include <functional>
#include <optional>
#include <string>

namespace proxy {

class ProxyController {
public:
    struct UpstreamResponse {
        int statusCode{200};
        std::string body;
        std::string headers;
    };

    struct ProxyResponse {
        FilterDecision decision{FilterDecision::BLOCK};
        int statusCode{500};
        std::string body;
        std::string headers;
        bool cacheHit{false};
        std::optional<int> matchedRuleId;
        int responseTimeMs{0};
    };

    using AsyncFetchCallback = std::function<void(std::optional<UpstreamResponse>)>;
    using AsyncFetcher = std::function<void(const std::string& url, const std::string& method, AsyncFetchCallback)>;

    ProxyController(FilterService& filterService, CacheService& cacheService, DBService& dbService, AppLogger& logger);

    void handleRequestAsync(
        const std::string& url,
        const std::string& method,
        const std::string& clientIp,
        const AsyncFetcher& fetcher,
        std::function<void(ProxyResponse)> completion);

    [[nodiscard]] FilterDecision handleRequest(const std::string& url, const std::string& method, const std::string& clientIp);
    [[nodiscard]] std::optional<std::string> getCachedResponse(const std::string& url, const std::string& method);
    [[nodiscard]] std::string makeCacheKey(const std::string& url, const std::string& method) const;

private:
    static std::string buildCacheKey(const std::string& url, const std::string& method);
    static std::string simulateOriginResponse(const std::string& url, const std::string& method);

    FilterService& filterService;
    CacheService& cacheService;
    DBService& dbService;
    AppLogger& logger;
};

}  // namespace proxy
