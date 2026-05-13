#pragma once

#include "proxy/DBService.h"

#include <string>
#include <vector>

namespace proxy {

class AdminUI {
public:
    AdminUI(DBService& dbService, AppLogger& logger);

    [[nodiscard]] std::vector<WhitelistRule> listWhitelistRules() const;
    void createWhitelistRule(WhitelistRule rule);
    void updateWhitelistRule(WhitelistRule rule);
    void deleteWhitelistRule(int ruleId);
    [[nodiscard]] std::vector<RequestRecord> listRequests() const;
    [[nodiscard]] std::vector<AdminUser> listAdminUsers() const;

    void exportRequestsCsv(const std::string& filePath) const;

private:
    static std::string csvEscape(const std::string& value);

    DBService& dbService;
    AppLogger& logger;
};

}  // namespace proxy
