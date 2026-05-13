#pragma once

#include "proxy/Types.h"

#include <string>

namespace proxy {

class WhitelistRule {
public:
    WhitelistRule() = default;
    WhitelistRule(int id, std::string urlPattern, MatchType matchType, bool enabled, std::string comment);

    [[nodiscard]] int getId() const;
    [[nodiscard]] const std::string& getUrlPattern() const;
    [[nodiscard]] MatchType getMatchType() const;
    [[nodiscard]] bool getEnabled() const;
    [[nodiscard]] const std::string& getComment() const;
    [[nodiscard]] const DateTime& getCreatedAt() const;
    [[nodiscard]] const DateTime& getUpdatedAt() const;

    void setId(int id);
    void setUrlPattern(std::string urlPattern);
    void setMatchType(MatchType matchType);
    void setEnabled(bool enabled);
    void setComment(std::string comment);
    void setCreatedAt(const DateTime& createdAt);
    void setUpdatedAt(const DateTime& updatedAt);

    [[nodiscard]] bool matches(const std::string& url) const;

private:
    int id{0};
    std::string urlPattern;
    MatchType matchType{MatchType::EXACT};
    bool enabled{true};
    std::string comment;
    DateTime createdAt{nowUtc()};
    DateTime updatedAt{nowUtc()};
};

}  // namespace proxy
