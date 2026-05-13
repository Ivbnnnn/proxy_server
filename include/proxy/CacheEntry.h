#pragma once

#include "proxy/Types.h"

#include <optional>
#include <string>

namespace proxy {

class CacheEntry {
public:
    CacheEntry() = default;
    CacheEntry(
        std::string cacheKey,
        std::string url,
        std::string method,
        int statusCode,
        std::string body,
        std::string headers,
        int ttlSeconds);

    [[nodiscard]] const std::string& getCacheKey() const;
    [[nodiscard]] const std::string& getUrl() const;
    [[nodiscard]] const std::string& getMethod() const;
    [[nodiscard]] int getStatusCode() const;
    [[nodiscard]] const std::string& getBody() const;
    [[nodiscard]] const std::string& getHeaders() const;
    [[nodiscard]] int getTtlSeconds() const;
    [[nodiscard]] const DateTime& getCreatedAt() const;
    [[nodiscard]] const DateTime& getExpiresAt() const;
    [[nodiscard]] const std::optional<DateTime>& getLastAccessAt() const;
    [[nodiscard]] int getHitCount() const;

    void setCacheKey(std::string cacheKey);
    void setUrl(std::string url);
    void setMethod(std::string method);
    void setStatusCode(int statusCode);
    void setBody(std::string body);
    void setHeaders(std::string headers);
    void setTtlSeconds(int ttlSeconds);
    void setCreatedAt(const DateTime& createdAt);
    void setExpiresAt(const DateTime& expiresAt);
    void setLastAccessAt(const std::optional<DateTime>& lastAccessAt);
    void setHitCount(int hitCount);

    [[nodiscard]] bool isExpired(const DateTime& now) const;
    void touch();

private:
    std::string cacheKey;
    std::string url;
    std::string method;
    int statusCode{200};
    std::string body;
    std::string headers;
    int ttlSeconds{60};
    DateTime createdAt{nowUtc()};
    DateTime expiresAt{nowUtc()};
    std::optional<DateTime> lastAccessAt;
    int hitCount{0};
};

}  // namespace proxy
