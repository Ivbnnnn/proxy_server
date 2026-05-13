#include "proxy/FilterService.h"

namespace proxy {

FilterService::FilterService(DBService& dbServiceValue, AppLogger& loggerValue)
    : dbService(dbServiceValue), logger(loggerValue) {
    reloadRules();
}

void FilterService::reloadRules() {
    rules = dbService.loadWhitelistRules();
    lastReloadAt = nowUtc();
    logger.info("Whitelist rules reloaded: " + std::to_string(rules.size()));
}

bool FilterService::isAllowed(const std::string& url) const { return check(url) == FilterDecision::ALLOW; }

FilterDecision FilterService::check(const std::string& url) const {
    auto match = findMatchingRule(url);
    if (match.has_value()) {
        return FilterDecision::ALLOW;
    }
    return FilterDecision::BLOCK;
}

std::optional<WhitelistRule> FilterService::findMatchingRule(const std::string& url) const {
    for (const auto& rule : rules) {
        if (!rule.getEnabled()) {
            continue;
        }
        if (rule.matches(url)) {
            return rule;
        }
    }
    return std::nullopt;
}

}  // namespace proxy
