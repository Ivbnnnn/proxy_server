#include "proxy/AdminUI.h"

#include <fstream>
#include <sstream>

namespace proxy {

AdminUI::AdminUI(DBService& dbServiceValue, AppLogger& loggerValue) : dbService(dbServiceValue), logger(loggerValue) {}

std::vector<WhitelistRule> AdminUI::listWhitelistRules() const { return dbService.loadWhitelistRules(); }

void AdminUI::createWhitelistRule(WhitelistRule rule) {
    dbService.saveWhitelistRule(std::move(rule));
    logger.info("Whitelist rule created");
}

void AdminUI::updateWhitelistRule(WhitelistRule rule) {
    dbService.saveWhitelistRule(std::move(rule));
    logger.info("Whitelist rule updated");
}

void AdminUI::deleteWhitelistRule(int ruleId) {
    dbService.deleteWhitelistRule(ruleId);
    logger.warn("Whitelist rule deleted: id=" + std::to_string(ruleId));
}

std::vector<RequestRecord> AdminUI::listRequests() const { return dbService.loadRequests(); }

std::vector<AdminUser> AdminUI::listAdminUsers() const { return dbService.loadAdminUsers(); }

void AdminUI::exportRequestsCsv(const std::string& filePath) const {
    std::ofstream out(filePath, std::ios::trunc);
    if (!out.is_open()) {
        logger.error("Failed to export requests to CSV. filePath=" + filePath);
        return;
    }

    // UTF-8 BOM helps spreadsheet apps on Windows open Cyrillic and semicolon-separated CSV correctly.
    out << "\xEF\xBB\xBF";
    out << "id;url;method;clientIp;statusCode;allowed;cacheHit;matchedRuleId;responseTimeMs;requestedAt\n";
    for (const auto& record : dbService.loadRequests()) {
        out << record.getId() << ";";
        out << csvEscape(record.getUrl()) << ";";
        out << csvEscape(record.getMethod()) << ";";
        out << csvEscape(record.getClientIp()) << ";";
        out << record.getStatusCode() << ";";
        out << (record.getAllowed() ? "true" : "false") << ";";
        out << (record.getCacheHit() ? "true" : "false") << ";";
        out << (record.getMatchedRuleId().has_value() ? std::to_string(*record.getMatchedRuleId()) : "") << ";";
        out << record.getResponseTimeMs() << ";";
        out << csvEscape(toIso8601(record.getRequestedAt())) << "\n";
    }
    logger.info("Requests exported to CSV: " + filePath);
}

std::string AdminUI::csvEscape(const std::string& value) {
    bool needQuotes = false;
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char ch : value) {
        if (ch == '"' || ch == ';' || ch == '\n' || ch == '\r') {
            needQuotes = true;
        }
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped.push_back(ch);
        }
    }
    if (needQuotes) {
        return "\"" + escaped + "\"";
    }
    return escaped;
}

}  // namespace proxy
