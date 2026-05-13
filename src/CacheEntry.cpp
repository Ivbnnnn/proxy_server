#include "proxy/CacheEntry.h"

#include <utility>

namespace proxy {

CacheEntry::CacheEntry(
    std::string cacheKeyValue,
    std::string urlValue,
    std::string methodValue,
    int statusCodeValue,
    std::string bodyValue,
    std::string headersValue,
    int ttlSecondsValue)
    : cacheKey(std::move(cacheKeyValue)),
      url(std::move(urlValue)),
      method(std::move(methodValue)),
      statusCode(statusCodeValue),
      body(std::move(bodyValue)),
      headers(std::move(headersValue)),
      ttlSeconds(ttlSecondsValue),
      createdAt(nowUtc()),
      expiresAt(createdAt + std::chrono::seconds(ttlSecondsValue)) {}

const std::string& CacheEntry::getCacheKey() const { return cacheKey; }

const std::string& CacheEntry::getUrl() const { return url; }

const std::string& CacheEntry::getMethod() const { return method; }

int CacheEntry::getStatusCode() const { return statusCode; }

const std::string& CacheEntry::getBody() const { return body; }

const std::string& CacheEntry::getHeaders() const { return headers; }

int CacheEntry::getTtlSeconds() const { return ttlSeconds; }

const DateTime& CacheEntry::getCreatedAt() const { return createdAt; }

const DateTime& CacheEntry::getExpiresAt() const { return expiresAt; }

const std::optional<DateTime>& CacheEntry::getLastAccessAt() const { return lastAccessAt; }

int CacheEntry::getHitCount() const { return hitCount; }

void CacheEntry::setCacheKey(std::string cacheKeyValue) { cacheKey = std::move(cacheKeyValue); }

void CacheEntry::setUrl(std::string urlValue) { url = std::move(urlValue); }

void CacheEntry::setMethod(std::string methodValue) { method = std::move(methodValue); }

void CacheEntry::setStatusCode(int statusCodeValue) { statusCode = statusCodeValue; }

void CacheEntry::setBody(std::string bodyValue) { body = std::move(bodyValue); }

void CacheEntry::setHeaders(std::string headersValue) { headers = std::move(headersValue); }

void CacheEntry::setTtlSeconds(int ttlSecondsValue) {
    ttlSeconds = ttlSecondsValue;
    expiresAt = createdAt + std::chrono::seconds(ttlSecondsValue);
}

void CacheEntry::setCreatedAt(const DateTime& createdAtValue) { createdAt = createdAtValue; }

void CacheEntry::setExpiresAt(const DateTime& expiresAtValue) { expiresAt = expiresAtValue; }

void CacheEntry::setLastAccessAt(const std::optional<DateTime>& lastAccessAtValue) { lastAccessAt = lastAccessAtValue; }

void CacheEntry::setHitCount(int hitCountValue) { hitCount = hitCountValue; }

bool CacheEntry::isExpired(const DateTime& now) const { return now >= expiresAt; }

void CacheEntry::touch() {
    lastAccessAt = nowUtc();
    ++hitCount;
}

}  // namespace proxy
