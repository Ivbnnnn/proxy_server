#include "proxy/DBService.h"

#include <stdexcept>

namespace proxy {

DBService::DBService(std::string connectionStringValue, int poolSizeValue, AppLogger& loggerValue)
    : connectionString(std::move(connectionStringValue)), poolSize(poolSizeValue), logger(loggerValue) {
    connection = std::make_unique<pqxx::connection>(connectionString);
    if (!connection || !connection->is_open()) {
        throw std::runtime_error("Failed to connect to PostgreSQL");
    }
    initSchema();
    logger.info("DBService connected to PostgreSQL. poolSize=" + std::to_string(poolSize));
}

std::vector<WhitelistRule> DBService::loadWhitelistRules() const {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    const auto result = tx.exec(
        "SELECT id,url_pattern,match_type,enabled,comment,created_at,updated_at "
        "FROM whitelist_rules ORDER BY id ASC");
    tx.commit();

    std::vector<WhitelistRule> rows;
    rows.reserve(result.size());
    for (const auto& row : result) {
        WhitelistRule rule;
        rule.setId(row["id"].as<int>());
        rule.setUrlPattern(row["url_pattern"].as<std::string>());
        rule.setMatchType(static_cast<MatchType>(row["match_type"].as<int>(0)));
        rule.setEnabled(row["enabled"].as<bool>(true));
        rule.setComment(row["comment"].as<std::string>(""));
        rule.setCreatedAt(fromUnixSeconds(row["created_at"].as<long long>(toUnixSeconds(nowUtc()))));
        rule.setUpdatedAt(fromUnixSeconds(row["updated_at"].as<long long>(toUnixSeconds(nowUtc()))));
        rows.push_back(std::move(rule));
    }
    return rows;
}

void DBService::saveRequest(RequestRecord record) {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    const std::string matchedRuleIdSql =
        record.getMatchedRuleId().has_value() ? std::to_string(*record.getMatchedRuleId()) : "NULL";
    const std::string query =
        "INSERT INTO request_records("
        "url,method,client_ip,status_code,allowed,cache_hit,matched_rule_id,response_time_ms,requested_at"
        ") VALUES(" +
        tx.quote(record.getUrl()) + "," + tx.quote(record.getMethod()) + "," + tx.quote(record.getClientIp()) + "," +
        tx.quote(record.getStatusCode()) + "," + tx.quote(record.getAllowed()) + "," + tx.quote(record.getCacheHit()) +
        "," + matchedRuleIdSql + "," + tx.quote(record.getResponseTimeMs()) + "," +
        tx.quote(toUnixSeconds(record.getRequestedAt())) + ") RETURNING id";
    const auto result = tx.exec(query);
    if (!result.empty()) {
        record.setId(result[0]["id"].as<int>());
    }
    tx.commit();
}

void DBService::saveAdminUser(AdminUser user) {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    const std::string lastLoginSql =
        user.getLastLoginAt().has_value() ? tx.quote(toUnixSeconds(*user.getLastLoginAt())) : "NULL";
    const std::string query =
        "INSERT INTO admin_users("
        "login,password_hash,role,is_active,created_at,last_login_at"
        ") VALUES(" +
        tx.quote(user.getLogin()) + "," + tx.quote(user.getPasswordHash()) + "," + tx.quote(user.getRole()) + "," +
        tx.quote(user.getIsActive()) + "," + tx.quote(toUnixSeconds(user.getCreatedAt())) + "," + lastLoginSql +
        ") "
        "ON CONFLICT(login) DO UPDATE SET "
        "password_hash=EXCLUDED.password_hash,"
        "role=EXCLUDED.role,"
        "is_active=EXCLUDED.is_active,"
        "last_login_at=EXCLUDED.last_login_at "
        "RETURNING id";
    const auto result = tx.exec(query);
    if (!result.empty()) {
        user.setId(result[0]["id"].as<int>());
    }
    tx.commit();
}

std::optional<AdminUser> DBService::getAdminUser(const std::string& login) const {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    const auto result = tx.exec(
        "SELECT id,login,password_hash,role,is_active,created_at,last_login_at "
        "FROM admin_users WHERE login=" +
        tx.quote(login) + " LIMIT 1");
    tx.commit();
    if (result.empty()) {
        return std::nullopt;
    }

    const auto& row = result[0];
    AdminUser user;
    user.setId(row["id"].as<int>());
    user.setLogin(row["login"].as<std::string>());
    user.setPasswordHash(row["password_hash"].as<std::string>());
    user.setRole(row["role"].as<std::string>("ADMIN"));
    user.setIsActive(row["is_active"].as<bool>(true));
    user.setCreatedAt(fromUnixSeconds(row["created_at"].as<long long>(toUnixSeconds(nowUtc()))));
    if (row["last_login_at"].is_null()) {
        user.setLastLoginAt(std::nullopt);
    } else {
        user.setLastLoginAt(fromUnixSeconds(row["last_login_at"].as<long long>()));
    }
    return user;
}

std::vector<RequestRecord> DBService::loadRequests() const {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    const auto result = tx.exec(
        "SELECT id,url,method,client_ip,status_code,allowed,cache_hit,matched_rule_id,response_time_ms,requested_at "
        "FROM request_records ORDER BY id DESC");
    tx.commit();

    std::vector<RequestRecord> rows;
    rows.reserve(result.size());
    for (const auto& row : result) {
        RequestRecord record;
        record.setId(row["id"].as<int>());
        record.setUrl(row["url"].as<std::string>());
        record.setMethod(row["method"].as<std::string>());
        record.setClientIp(row["client_ip"].as<std::string>());
        record.setStatusCode(row["status_code"].as<int>(0));
        record.setAllowed(row["allowed"].as<bool>(false));
        record.setCacheHit(row["cache_hit"].as<bool>(false));
        if (row["matched_rule_id"].is_null()) {
            record.setMatchedRuleId(std::nullopt);
        } else {
            record.setMatchedRuleId(row["matched_rule_id"].as<int>());
        }
        record.setResponseTimeMs(row["response_time_ms"].as<int>(0));
        record.setRequestedAt(fromUnixSeconds(row["requested_at"].as<long long>(toUnixSeconds(nowUtc()))));
        rows.push_back(std::move(record));
    }
    return rows;
}

std::vector<AdminUser> DBService::loadAdminUsers() const {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    const auto result = tx.exec(
        "SELECT id,login,password_hash,role,is_active,created_at,last_login_at "
        "FROM admin_users ORDER BY id ASC");
    tx.commit();

    std::vector<AdminUser> rows;
    rows.reserve(result.size());
    for (const auto& row : result) {
        AdminUser user;
        user.setId(row["id"].as<int>());
        user.setLogin(row["login"].as<std::string>());
        user.setPasswordHash(row["password_hash"].as<std::string>());
        user.setRole(row["role"].as<std::string>("ADMIN"));
        user.setIsActive(row["is_active"].as<bool>(true));
        user.setCreatedAt(fromUnixSeconds(row["created_at"].as<long long>(toUnixSeconds(nowUtc()))));
        if (row["last_login_at"].is_null()) {
            user.setLastLoginAt(std::nullopt);
        } else {
            user.setLastLoginAt(fromUnixSeconds(row["last_login_at"].as<long long>()));
        }
        rows.push_back(std::move(user));
    }
    return rows;
}

void DBService::saveWhitelistRule(WhitelistRule rule) {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    if (rule.getId() <= 0) {
        rule.setCreatedAt(nowUtc());
        rule.setUpdatedAt(nowUtc());
        const auto result = tx.exec(
            "INSERT INTO whitelist_rules("
            "url_pattern,match_type,enabled,comment,created_at,updated_at"
            ") VALUES(" +
            tx.quote(rule.getUrlPattern()) + "," + tx.quote(static_cast<int>(rule.getMatchType())) + "," +
            tx.quote(rule.getEnabled()) + "," + tx.quote(rule.getComment()) + "," +
            tx.quote(toUnixSeconds(rule.getCreatedAt())) + "," + tx.quote(toUnixSeconds(rule.getUpdatedAt())) +
            ") RETURNING id");
        if (!result.empty()) {
            rule.setId(result[0]["id"].as<int>());
        }
    } else {
        rule.setUpdatedAt(nowUtc());
        tx.exec0(
            "UPDATE whitelist_rules SET "
            "url_pattern=" +
            tx.quote(rule.getUrlPattern()) + ",match_type=" + tx.quote(static_cast<int>(rule.getMatchType())) +
            ",enabled=" + tx.quote(rule.getEnabled()) + ",comment=" + tx.quote(rule.getComment()) +
            ",updated_at=" + tx.quote(toUnixSeconds(rule.getUpdatedAt())) + " WHERE id=" + tx.quote(rule.getId()));
    }
    tx.commit();
}

void DBService::deleteWhitelistRule(int ruleId) {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    tx.exec0("DELETE FROM whitelist_rules WHERE id=" + tx.quote(ruleId));
    tx.commit();
}

void DBService::initSchema() {
    std::lock_guard<std::mutex> lock(mutex);
    pqxx::work tx(*connection);
    tx.exec0(
        "CREATE TABLE IF NOT EXISTS admin_users ("
        "id SERIAL PRIMARY KEY,"
        "login TEXT UNIQUE NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL DEFAULT 'ADMIN',"
        "is_active BOOLEAN NOT NULL DEFAULT TRUE,"
        "created_at BIGINT NOT NULL,"
        "last_login_at BIGINT"
        ")");
    tx.exec0(
        "CREATE TABLE IF NOT EXISTS whitelist_rules ("
        "id SERIAL PRIMARY KEY,"
        "url_pattern TEXT NOT NULL,"
        "match_type INT NOT NULL,"
        "enabled BOOLEAN NOT NULL DEFAULT TRUE,"
        "comment TEXT NOT NULL DEFAULT '',"
        "created_at BIGINT NOT NULL,"
        "updated_at BIGINT NOT NULL"
        ")");
    tx.exec0(
        "CREATE TABLE IF NOT EXISTS request_records ("
        "id SERIAL PRIMARY KEY,"
        "url TEXT NOT NULL,"
        "method TEXT NOT NULL,"
        "client_ip TEXT NOT NULL,"
        "status_code INT NOT NULL,"
        "allowed BOOLEAN NOT NULL,"
        "cache_hit BOOLEAN NOT NULL,"
        "matched_rule_id INT,"
        "response_time_ms INT NOT NULL,"
        "requested_at BIGINT NOT NULL"
        ")");
    tx.commit();
}

}  // namespace proxy
