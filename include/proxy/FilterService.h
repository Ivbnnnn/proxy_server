#pragma once

#include "proxy/DBService.h"
#include "proxy/Types.h"

#include <optional>
#include <vector>

namespace proxy {

class FilterService {
public:
    FilterService(DBService& dbService, AppLogger& logger);

    void reloadRules();
    [[nodiscard]] bool isAllowed(const std::string& url) const;
    [[nodiscard]] FilterDecision check(const std::string& url) const;
    [[nodiscard]] std::optional<WhitelistRule> findMatchingRule(const std::string& url) const;

private:
    DBService& dbService;
    AppLogger& logger;
    std::vector<WhitelistRule> rules;
    std::optional<DateTime> lastReloadAt;
};

}  // namespace proxy
