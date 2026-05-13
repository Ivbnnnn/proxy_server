#pragma once

#include "proxy/AdminUser.h"
#include "proxy/AppLogger.h"
#include "proxy/RequestRecord.h"
#include "proxy/WhitelistRule.h"

#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <pqxx/pqxx>

namespace proxy {

class DBService {
public:
    DBService(std::string connectionString, int poolSize, AppLogger& logger);

    [[nodiscard]] std::vector<WhitelistRule> loadWhitelistRules() const;
    void saveRequest(RequestRecord record);
    void saveAdminUser(AdminUser user);
    [[nodiscard]] std::optional<AdminUser> getAdminUser(const std::string& login) const;
    [[nodiscard]] std::vector<RequestRecord> loadRequests() const;
    [[nodiscard]] std::vector<AdminUser> loadAdminUsers() const;
    void saveWhitelistRule(WhitelistRule rule);
    void deleteWhitelistRule(int ruleId);

private:
    void initSchema();

    std::string connectionString;
    int poolSize{1};
    AppLogger& logger;

    std::unique_ptr<pqxx::connection> connection;

    mutable std::mutex mutex;
};

}  // namespace proxy
