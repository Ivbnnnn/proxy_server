#include "proxy/WhitelistRule.h"

#include <regex>

namespace proxy {

WhitelistRule::WhitelistRule(
    int idValue,
    std::string urlPatternValue,
    MatchType matchTypeValue,
    bool enabledValue,
    std::string commentValue)
    : id(idValue),
      urlPattern(std::move(urlPatternValue)),
      matchType(matchTypeValue),
      enabled(enabledValue),
      comment(std::move(commentValue)),
      createdAt(nowUtc()),
      updatedAt(nowUtc()) {}

int WhitelistRule::getId() const { return id; }

const std::string& WhitelistRule::getUrlPattern() const { return urlPattern; }

MatchType WhitelistRule::getMatchType() const { return matchType; }

bool WhitelistRule::getEnabled() const { return enabled; }

const std::string& WhitelistRule::getComment() const { return comment; }

const DateTime& WhitelistRule::getCreatedAt() const { return createdAt; }

const DateTime& WhitelistRule::getUpdatedAt() const { return updatedAt; }

void WhitelistRule::setId(int idValue) { id = idValue; }

void WhitelistRule::setUrlPattern(std::string urlPatternValue) { urlPattern = std::move(urlPatternValue); }

void WhitelistRule::setMatchType(MatchType matchTypeValue) { matchType = matchTypeValue; }

void WhitelistRule::setEnabled(bool enabledValue) { enabled = enabledValue; }

void WhitelistRule::setComment(std::string commentValue) { comment = std::move(commentValue); }

void WhitelistRule::setCreatedAt(const DateTime& createdAtValue) { createdAt = createdAtValue; }

void WhitelistRule::setUpdatedAt(const DateTime& updatedAtValue) { updatedAt = updatedAtValue; }

bool WhitelistRule::matches(const std::string& url) const {
    if (!enabled || urlPattern.empty()) {
        return false;
    }

    const std::string urlLower = toLower(url);
    const std::string patternLower = toLower(urlPattern);

    switch (matchType) {
        case MatchType::EXACT:
            return urlLower == patternLower;
        case MatchType::PREFIX:
            return urlLower.rfind(patternLower, 0) == 0;
        case MatchType::CONTAINS:
            return urlLower.find(patternLower) != std::string::npos;
        case MatchType::REGEX: {
            try {
                const std::regex expr(urlPattern, std::regex::icase);
                return std::regex_search(url, expr);
            } catch (...) {
                return false;
            }
        }
        default:
            return false;
    }
}

}  // namespace proxy
