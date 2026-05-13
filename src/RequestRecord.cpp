#include "proxy/RequestRecord.h"

#include <utility>

namespace proxy {

int RequestRecord::getId() const { return id; }

const std::string& RequestRecord::getUrl() const { return url; }

const std::string& RequestRecord::getMethod() const { return method; }

const std::string& RequestRecord::getClientIp() const { return clientIp; }

int RequestRecord::getStatusCode() const { return statusCode; }

bool RequestRecord::getAllowed() const { return allowed; }

bool RequestRecord::getCacheHit() const { return cacheHit; }

const std::optional<int>& RequestRecord::getMatchedRuleId() const { return matchedRuleId; }

int RequestRecord::getResponseTimeMs() const { return responseTimeMs; }

const DateTime& RequestRecord::getRequestedAt() const { return requestedAt; }

void RequestRecord::setId(int idValue) { id = idValue; }

void RequestRecord::setUrl(std::string urlValue) { url = std::move(urlValue); }

void RequestRecord::setMethod(std::string methodValue) { method = std::move(methodValue); }

void RequestRecord::setClientIp(std::string clientIpValue) { clientIp = std::move(clientIpValue); }

void RequestRecord::setStatusCode(int statusCodeValue) { statusCode = statusCodeValue; }

void RequestRecord::setAllowed(bool allowedValue) { allowed = allowedValue; }

void RequestRecord::setCacheHit(bool cacheHitValue) { cacheHit = cacheHitValue; }

void RequestRecord::setMatchedRuleId(const std::optional<int>& matchedRuleIdValue) { matchedRuleId = matchedRuleIdValue; }

void RequestRecord::setResponseTimeMs(int responseTimeMsValue) { responseTimeMs = responseTimeMsValue; }

void RequestRecord::setRequestedAt(const DateTime& requestedAtValue) { requestedAt = requestedAtValue; }

}  // namespace proxy
