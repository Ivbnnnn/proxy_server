#include "proxy/ProxyController.h"

#include <chrono>
#include <future>

namespace proxy {

ProxyController::ProxyController(
    FilterService& filterServiceValue,
    CacheService& cacheServiceValue,
    DBService& dbServiceValue,
    AppLogger& loggerValue)
    : filterService(filterServiceValue), cacheService(cacheServiceValue), dbService(dbServiceValue), logger(loggerValue) {}

void ProxyController::handleRequestAsync(
    const std::string& url,
    const std::string& method,
    const std::string& clientIp,
    const AsyncFetcher& fetcher,
    std::function<void(ProxyResponse)> completion) {
    const auto startedAt = std::chrono::steady_clock::now();
    const auto matchedRule = filterService.findMatchingRule(url);

    auto persistAndComplete = [this,
                               url,
                               method,
                               clientIp,
                               startedAt,
                               completion = std::move(completion)](ProxyResponse response) {
            RequestRecord record;
            record.setUrl(url);
            record.setMethod(method);
            record.setClientIp(clientIp);
            record.setRequestedAt(nowUtc());
            record.setStatusCode(response.statusCode);
            record.setAllowed(response.decision == FilterDecision::ALLOW);
            record.setCacheHit(response.cacheHit);
            record.setMatchedRuleId(response.matchedRuleId);
            record.setResponseTimeMs(
                static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt)
                        .count()));
            response.responseTimeMs = record.getResponseTimeMs();
            dbService.saveRequest(record);
            completion(std::move(response));
        };

    if (!matchedRule.has_value()) {
        ProxyResponse blocked;
        blocked.decision = FilterDecision::BLOCK;
        blocked.statusCode = 403;
        blocked.body = "URL is blocked by whitelist policy";
        blocked.headers = "Content-Type:text/plain";
        blocked.cacheHit = false;
        blocked.matchedRuleId = std::nullopt;
        logger.warn("Blocked request url=" + url + ", ip=" + clientIp);
        persistAndComplete(std::move(blocked));
        return;
    }

    const std::string cacheKey = buildCacheKey(url, method);
    const auto cached = cacheService.get(cacheKey);
    if (cached.has_value() && !cached->isExpired(nowUtc())) {
        ProxyResponse response;
        response.decision = FilterDecision::ALLOW;
        response.statusCode = cached->getStatusCode();
        response.body = cached->getBody();
        response.headers = cached->getHeaders();
        response.cacheHit = true;
        response.matchedRuleId = matchedRule->getId();
        logger.info("Request served from cache url=" + url + ", ip=" + clientIp);
        persistAndComplete(std::move(response));
        return;
    }

    if (!fetcher) {
        ProxyResponse response;
        response.decision = FilterDecision::ALLOW;
        response.statusCode = 500;
        response.body = "Upstream fetcher is not configured";
        response.headers = "Content-Type:text/plain";
        response.cacheHit = false;
        response.matchedRuleId = matchedRule->getId();
        logger.error("Fetcher is not configured for url=" + url);
        persistAndComplete(std::move(response));
        return;
    }

    fetcher(url, method, [this, cacheKey, url, method, clientIp, matchedRule, persistAndComplete = std::move(persistAndComplete)](
                             std::optional<UpstreamResponse> upstream) mutable {
        ProxyResponse response;
        response.decision = FilterDecision::ALLOW;
        response.cacheHit = false;
        response.matchedRuleId = matchedRule->getId();

        if (!upstream.has_value()) {
            response.statusCode = 502;
            response.body = "Failed to fetch upstream response";
            response.headers = "Content-Type:text/plain";
            logger.error("Upstream fetch failed url=" + url + ", ip=" + clientIp);
            persistAndComplete(std::move(response));
            return;
        }

        response.statusCode = upstream->statusCode;
        response.body = upstream->body;
        response.headers = upstream->headers;

        if (response.statusCode < 500) {
            CacheEntry freshEntry(
                cacheKey,
                url,
                method,
                response.statusCode,
                response.body,
                response.headers,
                cacheService.getDefaultTtlSeconds());

            cacheService.put(freshEntry);
            logger.info("Request forwarded and cached url=" + url + ", ip=" + clientIp);
        } else {
            logger.warn("Request forwarded without cache url=" + url + ", status=" + std::to_string(response.statusCode));
        }
        persistAndComplete(std::move(response));
    });
}

FilterDecision ProxyController::handleRequest(const std::string& url, const std::string& method, const std::string& clientIp) {
    std::promise<FilterDecision> promise;
    auto future = promise.get_future();
    handleRequestAsync(
        url,
        method,
        clientIp,
        [](const std::string& fetchUrl, const std::string& fetchMethod, AsyncFetchCallback callback) {
            UpstreamResponse upstream;
            upstream.statusCode = 200;
            upstream.body = simulateOriginResponse(fetchUrl, fetchMethod);
            upstream.headers = "Content-Type:text/plain";
            callback(upstream);
        },
        [&promise](ProxyResponse response) { promise.set_value(response.decision); });
    return future.get();
}

std::optional<std::string> ProxyController::getCachedResponse(const std::string& url, const std::string& method) {
    const auto entry = cacheService.get(buildCacheKey(url, method));
    if (!entry.has_value()) {
        return std::nullopt;
    }
    return entry->getBody();
}

std::string ProxyController::makeCacheKey(const std::string& url, const std::string& method) const {
    return buildCacheKey(url, method);
}

std::string ProxyController::buildCacheKey(const std::string& url, const std::string& method) {
    return toLower(method) + ":" + url;
}

std::string ProxyController::simulateOriginResponse(const std::string& url, const std::string& method) {
    return "origin-response: method=" + method + ", url=" + url;
}

}  // namespace proxy
