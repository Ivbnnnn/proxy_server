#pragma once

#include "proxy/Types.h"

#include <optional>
#include <string>

namespace proxy {

class RequestRecord {
public:
    RequestRecord() = default;

    [[nodiscard]] int getId() const;
    [[nodiscard]] const std::string& getUrl() const;
    [[nodiscard]] const std::string& getMethod() const;
    [[nodiscard]] const std::string& getClientIp() const;
    [[nodiscard]] int getStatusCode() const;
    [[nodiscard]] bool getAllowed() const;
    [[nodiscard]] bool getCacheHit() const;
    [[nodiscard]] const std::optional<int>& getMatchedRuleId() const;
    [[nodiscard]] int getResponseTimeMs() const;
    [[nodiscard]] const DateTime& getRequestedAt() const;

    void setId(int id);
    void setUrl(std::string url);
    void setMethod(std::string method);
    void setClientIp(std::string clientIp);
    void setStatusCode(int statusCode);
    void setAllowed(bool allowed);
    void setCacheHit(bool cacheHit);
    void setMatchedRuleId(const std::optional<int>& matchedRuleId);
    void setResponseTimeMs(int responseTimeMs);
    void setRequestedAt(const DateTime& requestedAt);

private:
    int id{0};
    std::string url;
    std::string method;
    std::string clientIp;
    int statusCode{0};
    bool allowed{false};
    bool cacheHit{false};
    std::optional<int> matchedRuleId;
    int responseTimeMs{0};
    DateTime requestedAt{nowUtc()};
};

}  // namespace proxy
