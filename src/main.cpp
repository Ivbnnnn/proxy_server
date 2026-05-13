#include "proxy/AdminUI.h"
#include "proxy/CacheService.h"
#include "proxy/DBService.h"
#include "proxy/FilterService.h"
#include "proxy/ProxyController.h"
#include "proxy/RedisService.h"
#include "proxy/WhitelistRule.h"

#include <drogon/drogon.h>

#include <cstdlib>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <iostream>
#include <vector>

namespace {

struct ParsedTarget {
    std::string host;
    std::string path;
};

constexpr const char* kExportCsvPath = "requests_export.csv";

void addCorsHeaders(const drogon::HttpResponsePtr& response) {
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type, Accept, Authorization, X-Requested-With");
    response->addHeader("Access-Control-Expose-Headers", "X-Proxy-Decision, X-Proxy-Cache-Hit, X-Proxy-Response-Time-Ms, X-Proxy-Upstream-Status");
}

drogon::HttpResponsePtr corsPreflightResponse() {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    addCorsHeaders(response);
    return response;
}

Json::Value requestToJson(const proxy::RequestRecord& record) {
    Json::Value out;
    out["id"] = record.getId();
    out["url"] = record.getUrl();
    out["method"] = record.getMethod();
    out["clientIp"] = record.getClientIp();
    out["statusCode"] = record.getStatusCode();
    out["allowed"] = record.getAllowed();
    out["cacheHit"] = record.getCacheHit();
    if (record.getMatchedRuleId().has_value()) {
        out["matchedRuleId"] = *record.getMatchedRuleId();
    } else {
        out["matchedRuleId"] = Json::nullValue;
    }
    out["responseTimeMs"] = record.getResponseTimeMs();
    out["requestedAt"] = proxy::toIso8601(record.getRequestedAt());
    return out;
}

std::optional<std::int64_t> parseOptionalInt64(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoll(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<ParsedTarget> parseTarget(const std::string& url) {
    static const std::regex urlRegex(R"(^(https?://[^/?#\s]+)([/?].*)?$)", std::regex::icase);
    std::smatch match;
    if (!std::regex_match(url, match, urlRegex)) {
        return std::nullopt;
    }

    ParsedTarget target;
    target.host = match[1].str();
    target.path = match[2].matched ? match[2].str() : "/";
    if (target.path.empty()) {
        target.path = "/";
    }
    if (target.path[0] == '?') {
        target.path = "/" + target.path;
    }
    return target;
}

drogon::HttpMethod parseHttpMethod(const std::string& method) {
    const std::string m = proxy::toLower(method);
    if (m == "post") {
        return drogon::Post;
    }
    if (m == "put") {
        return drogon::Put;
    }
    if (m == "delete") {
        return drogon::Delete;
    }
    if (m == "patch") {
        return drogon::Patch;
    }
    if (m == "head") {
        return drogon::Head;
    }
    if (m == "options") {
        return drogon::Options;
    }
    return drogon::Get;
}

template <typename HeaderMap>
std::string flattenHeaders(const HeaderMap& headers) {
    std::ostringstream oss;
    for (const auto& kv : headers) {
        oss << kv.first << ":" << kv.second << "\n";
    }
    return oss.str();
}

void applyFlattenedHeaders(const std::string& text, const drogon::HttpResponsePtr& response) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);
        const std::string keyLower = proxy::toLower(key);
        if (keyLower == "content-length" || keyLower == "transfer-encoding") {
            continue;
        }
        if (!key.empty() && !value.empty()) {
            response->addHeader(key, value);
        }
    }
}

std::optional<proxy::MatchType> parseMatchType(const Json::Value& value) {
    if (value.isInt()) {
        const int v = value.asInt();
        if (v >= 0 && v <= 3) {
            return static_cast<proxy::MatchType>(v);
        }
    }
    if (value.isString()) {
        const std::string s = proxy::toLower(value.asString());
        if (s == "exact") {
            return proxy::MatchType::EXACT;
        }
        if (s == "prefix") {
            return proxy::MatchType::PREFIX;
        }
        if (s == "contains") {
            return proxy::MatchType::CONTAINS;
        }
        if (s == "regex") {
            return proxy::MatchType::REGEX;
        }
    }
    return std::nullopt;
}

Json::Value ruleToJson(const proxy::WhitelistRule& rule) {
    Json::Value out;
    out["id"] = rule.getId();
    out["urlPattern"] = rule.getUrlPattern();
    out["matchType"] = proxy::toString(rule.getMatchType());
    out["enabled"] = rule.getEnabled();
    out["comment"] = rule.getComment();
    out["createdAt"] = proxy::toIso8601(rule.getCreatedAt());
    out["updatedAt"] = proxy::toIso8601(rule.getUpdatedAt());
    return out;
}

drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode status, const std::string& message) {
    Json::Value body;
    body["error"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    addCorsHeaders(resp);
    return resp;
}

}  // namespace

int main() {
    using namespace proxy;
    try {
        const char* dbConnEnv = std::getenv("PROXY_DB_CONN");
        const char* redisHostEnv = std::getenv("PROXY_REDIS_HOST");
        const char* redisPortEnv = std::getenv("PROXY_REDIS_PORT");
        const std::string dbConn = dbConnEnv
                                       ? dbConnEnv
                                       : "host=127.0.0.1 port=5433 dbname=proxy_db user=postgres password=postgres";
        const std::string redisHost = redisHostEnv ? redisHostEnv : "127.0.0.1";
        const int redisPort = redisPortEnv ? std::stoi(redisPortEnv) : 6379;

        AppLogger logger("http-proxy", LogLevel::DEBUG, std::nullopt, true);
        logger.info("Starting proxy with DB=" + dbConn + ", Redis=" + redisHost + ":" + std::to_string(redisPort));

        DBService dbService(dbConn, 8, logger);
        RedisService redisService(redisHost, redisPort, 0, std::nullopt, 1000, logger);

        CacheSettings cacheSettings;
        cacheSettings.defaultTtlSeconds = std::getenv("PROXY_CACHE_TTL")
            ? std::stoi(std::getenv("PROXY_CACHE_TTL"))
            : 120;
        cacheSettings.maxEntries = std::getenv("PROXY_CACHE_MAX_ENTRIES")
            ? static_cast<std::size_t>(std::stoull(std::getenv("PROXY_CACHE_MAX_ENTRIES")))
            : 1000;
        cacheSettings.maxBytes = std::getenv("PROXY_CACHE_MAX_BYTES")
            ? static_cast<std::size_t>(std::stoull(std::getenv("PROXY_CACHE_MAX_BYTES")))
            : 0;

        CacheService cacheService(redisService, logger, cacheSettings);

        FilterService filterService(dbService, logger);
        ProxyController proxyController(filterService, cacheService, dbService, logger);
        AdminUI adminUI(dbService, logger);

        if (!dbService.getAdminUser("admin").has_value()) {
            AdminUser admin(0, "admin", "sha256:demo_hash", true);
            dbService.saveAdminUser(admin);
        }
        filterService.reloadRules();

        const ProxyController::AsyncFetcher upstreamFetcher = [&logger](
                                                              const std::string& targetUrl,
                                                              const std::string& method,
                                                              ProxyController::AsyncFetchCallback callback) {
        const auto parsed = parseTarget(targetUrl);
        if (!parsed.has_value()) {
            logger.warn("Invalid target URL: " + targetUrl);
            callback(std::nullopt);
            return;
        }

        auto client = drogon::HttpClient::newHttpClient(parsed->host);
        auto outReq = drogon::HttpRequest::newHttpRequest();
        outReq->setMethod(parseHttpMethod(method));
        outReq->setPath(parsed->path);
        outReq->setPassThrough(true);

        client->sendRequest(
            outReq,
            [callback = std::move(callback)](drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
                if (result != drogon::ReqResult::Ok || !response) {
                    callback(std::nullopt);
                    return;
                }
                ProxyController::UpstreamResponse upstream;
                upstream.statusCode = static_cast<int>(response->statusCode());
                upstream.body = std::string(response->body());
                upstream.headers = flattenHeaders(response->headers());
                callback(std::move(upstream));
            });
    };

        drogon::app().registerPreRoutingAdvice(
            [](const drogon::HttpRequestPtr& req,
               drogon::AdviceCallback&& callback,
               drogon::AdviceChainCallback&& pass) {
                if (req->method() == drogon::Options) {
                    callback(corsPreflightResponse());
                    return;
                }
                pass();
            });

        drogon::app().registerHandler(
        "/proxy",
        [&proxyController, upstreamFetcher, &logger](const drogon::HttpRequestPtr& req,
                                            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP /proxy request received");
            const std::string targetUrl = req->getParameter("url");
            if (targetUrl.empty()) {
                logger.warn("HTTP /proxy missing required query param: url");
                callback(jsonError(drogon::k400BadRequest, "Query parameter 'url' is required"));
                return;
            }

            std::string method = req->getParameter("method");
            if (method.empty()) {
                method = "GET";
            }

            const std::string clientIp = req->peerAddr().toIp();
            proxyController.handleRequestAsync(
                targetUrl,
                method,
                clientIp,
                upstreamFetcher,
                [callback = std::move(callback), &logger, targetUrl](ProxyController::ProxyResponse result) mutable {
                    auto response = drogon::HttpResponse::newHttpResponse();
                    const bool upstreamRedirect = result.statusCode >= 300 && result.statusCode < 400;
                    response->setCustomStatusCode(upstreamRedirect ? 200 : result.statusCode);
                    response->setBody(result.body);
                    if (result.headers.empty()) {
                        response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
                    } else {
                        applyFlattenedHeaders(result.headers, response);
                        if (response->getHeader("content-type").empty()) {
                            response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
                        }
                    }
                    response->addHeader("X-Proxy-Decision", toString(result.decision));
                    response->addHeader("X-Proxy-Cache-Hit", result.cacheHit ? "true" : "false");
                    response->addHeader("X-Proxy-Upstream-Status", std::to_string(result.statusCode));
                    response->addHeader("X-Proxy-Response-Time-Ms", std::to_string(result.responseTimeMs));
                    addCorsHeaders(response);
                    logger.info(
                        "HTTP /proxy completed: url=" + targetUrl +
                        ", status=" + std::to_string(result.statusCode) +
                        ", decision=" + toString(result.decision) +
                        ", cacheHit=" + std::string(result.cacheHit ? "true" : "false"));
                    callback(response);
                });
        },
        {drogon::Get, drogon::Post, drogon::Put, drogon::Delete, drogon::Patch});

    drogon::app().registerHandler(
        "/proxy",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP /proxy OPTIONS preflight");
            callback(corsPreflightResponse());
        },
        {drogon::Options});

        drogon::app().registerHandler(
        "/admin/whitelist",
        [&adminUI, &logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP GET /admin/whitelist");
            Json::Value out(Json::arrayValue);
            for (const auto& rule : adminUI.listWhitelistRules()) {
                out.append(ruleToJson(rule));
            }
            auto response = drogon::HttpResponse::newHttpJsonResponse(out);
            addCorsHeaders(response);
            callback(response);
        },
        {drogon::Get});

        drogon::app().registerHandler(
        "/admin/whitelist",
        [&adminUI, &filterService, &logger](const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP POST /admin/whitelist");
            const auto json = req->getJsonObject();
            if (!json) {
                logger.warn("HTTP POST /admin/whitelist invalid JSON body");
                callback(jsonError(drogon::k400BadRequest, "JSON body is required"));
                return;
            }

            if (!json->isMember("urlPattern") || !(*json)["urlPattern"].isString()) {
                logger.warn("HTTP POST /admin/whitelist missing urlPattern");
                callback(jsonError(drogon::k400BadRequest, "Field 'urlPattern' is required and must be string"));
                return;
            }
            const auto matchType = json->isMember("matchType") ? parseMatchType((*json)["matchType"])
                                                               : std::optional<MatchType>(MatchType::EXACT);
            if (!matchType.has_value()) {
                logger.warn("HTTP POST /admin/whitelist invalid matchType");
                callback(jsonError(drogon::k400BadRequest, "Field 'matchType' must be EXACT|PREFIX|CONTAINS|REGEX or 0..3"));
                return;
            }

            WhitelistRule rule(
                0,
                (*json)["urlPattern"].asString(),
                *matchType,
                json->isMember("enabled") ? (*json)["enabled"].asBool() : true,
                json->isMember("comment") ? (*json)["comment"].asString() : "");
            adminUI.createWhitelistRule(rule);
            filterService.reloadRules();

            const auto all = adminUI.listWhitelistRules();
            const auto& created = all.back();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(ruleToJson(created));
            resp->setStatusCode(drogon::k201Created);
            addCorsHeaders(resp);
            logger.info("HTTP POST /admin/whitelist created rule id=" + std::to_string(created.getId()));
            callback(resp);
        },
        {drogon::Post});

        drogon::app().registerHandler(
        "/admin/whitelist",
        [&adminUI, &filterService, &logger](const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP PUT /admin/whitelist");
            const std::string idParam = req->getParameter("id");
            if (idParam.empty()) {
                logger.warn("HTTP PUT /admin/whitelist missing id");
                callback(jsonError(drogon::k400BadRequest, "Query parameter 'id' is required"));
                return;
            }

            int id = 0;
            try {
                id = std::stoi(idParam);
            } catch (...) {
                logger.warn("HTTP PUT /admin/whitelist invalid id=" + idParam);
                callback(jsonError(drogon::k400BadRequest, "Query parameter 'id' must be integer"));
                return;
            }

            const auto json = req->getJsonObject();
            if (!json) {
                logger.warn("HTTP PUT /admin/whitelist invalid JSON body");
                callback(jsonError(drogon::k400BadRequest, "JSON body is required"));
                return;
            }
            if (!json->isMember("urlPattern") || !(*json)["urlPattern"].isString()) {
                logger.warn("HTTP PUT /admin/whitelist missing urlPattern");
                callback(jsonError(drogon::k400BadRequest, "Field 'urlPattern' is required and must be string"));
                return;
            }

            const auto matchType = json->isMember("matchType") ? parseMatchType((*json)["matchType"])
                                                               : std::optional<MatchType>(MatchType::EXACT);
            if (!matchType.has_value()) {
                logger.warn("HTTP PUT /admin/whitelist invalid matchType");
                callback(jsonError(drogon::k400BadRequest, "Field 'matchType' must be EXACT|PREFIX|CONTAINS|REGEX or 0..3"));
                return;
            }

            WhitelistRule rule(
                id,
                (*json)["urlPattern"].asString(),
                *matchType,
                json->isMember("enabled") ? (*json)["enabled"].asBool() : true,
                json->isMember("comment") ? (*json)["comment"].asString() : "");
            adminUI.updateWhitelistRule(rule);
            filterService.reloadRules();

            Json::Value out;
            out["updated"] = true;
            out["id"] = id;
            auto response = drogon::HttpResponse::newHttpJsonResponse(out);
            addCorsHeaders(response);
            logger.info("HTTP PUT /admin/whitelist updated rule id=" + std::to_string(id));
            callback(response);
        },
        {drogon::Put});

        drogon::app().registerHandler(
        "/admin/whitelist",
        [&adminUI, &filterService, &logger](const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP DELETE /admin/whitelist");
            const std::string idParam = req->getParameter("id");
            if (idParam.empty()) {
                logger.warn("HTTP DELETE /admin/whitelist missing id");
                callback(jsonError(drogon::k400BadRequest, "Query parameter 'id' is required"));
                return;
            }

            int id = 0;
            try {
                id = std::stoi(idParam);
            } catch (...) {
                logger.warn("HTTP DELETE /admin/whitelist invalid id=" + idParam);
                callback(jsonError(drogon::k400BadRequest, "Query parameter 'id' must be integer"));
                return;
            }

            adminUI.deleteWhitelistRule(id);
            filterService.reloadRules();
            Json::Value out;
            out["deleted"] = true;
            out["id"] = id;
            auto response = drogon::HttpResponse::newHttpJsonResponse(out);
            addCorsHeaders(response);
            logger.info("HTTP DELETE /admin/whitelist deleted rule id=" + std::to_string(id));
            callback(response);
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        "/admin/export/requests",
        [&adminUI, &logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP GET /admin/export/requests");
            adminUI.exportRequestsCsv(kExportCsvPath);
            Json::Value out;
            out["exported"] = true;
            out["file"] = kExportCsvPath;
            auto response = drogon::HttpResponse::newHttpJsonResponse(out);
            addCorsHeaders(response);
            logger.info("HTTP GET /admin/export/requests exported file=" + std::string(kExportCsvPath));
            callback(response);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/admin/export/requests/download",
        [&adminUI, &logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP GET /admin/export/requests/download");
            adminUI.exportRequestsCsv(kExportCsvPath);

            std::ifstream file(kExportCsvPath, std::ios::binary);
            if (!file.is_open()) {
                logger.error("HTTP GET /admin/export/requests/download cannot open file");
                callback(jsonError(drogon::k500InternalServerError, "Cannot open export file"));
                return;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();

            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::k200OK);
            response->addHeader("Content-Type", "text/csv; charset=utf-8");
            response->setBody(buffer.str());
            response->addHeader("Content-Disposition", "attachment; filename=\"requests_export.csv\"");
            addCorsHeaders(response);
            logger.info("HTTP GET /admin/export/requests/download success");
            callback(response);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/admin/requests",
        [&adminUI, &logger](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP GET /admin/requests");
            const std::string urlFilter = proxy::toLower(req->getParameter("url"));
            const std::string ipFilter = proxy::toLower(req->getParameter("ip"));
            const std::string search = proxy::toLower(req->getParameter("search"));
            const std::optional<std::int64_t> fromTs = parseOptionalInt64(req->getParameter("from"));
            const std::optional<std::int64_t> toTs = parseOptionalInt64(req->getParameter("to"));

            Json::Value out(Json::arrayValue);
            for (const auto& record : adminUI.listRequests()) {
                const std::string recordUrl = proxy::toLower(record.getUrl());
                const std::string recordIp = proxy::toLower(record.getClientIp());
                const std::string recordMethod = proxy::toLower(record.getMethod());
                const std::int64_t requestedAtTs = proxy::toUnixSeconds(record.getRequestedAt());

                if (!urlFilter.empty() && recordUrl.find(urlFilter) == std::string::npos) {
                    continue;
                }
                if (!ipFilter.empty() && recordIp.find(ipFilter) == std::string::npos) {
                    continue;
                }
                if (fromTs.has_value() && requestedAtTs < *fromTs) {
                    continue;
                }
                if (toTs.has_value() && requestedAtTs > *toTs) {
                    continue;
                }
                if (!search.empty()) {
                    const bool matched =
                        recordUrl.find(search) != std::string::npos ||
                        recordIp.find(search) != std::string::npos ||
                        recordMethod.find(search) != std::string::npos;
                    if (!matched) {
                        continue;
                    }
                }
                out.append(requestToJson(record));
            }
            auto response = drogon::HttpResponse::newHttpJsonResponse(out);
            addCorsHeaders(response);
            logger.info("HTTP GET /admin/requests returned " + std::to_string(out.size()) + " rows");
            callback(response);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/admin/stats",
        [&adminUI, &logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP GET /admin/stats");
            const auto requests = adminUI.listRequests();
            int allowedCount = 0;
            int blockedCount = 0;
            int cacheHitCount = 0;
            for (const auto& item : requests) {
                if (item.getAllowed()) {
                    ++allowedCount;
                } else {
                    ++blockedCount;
                }
                if (item.getCacheHit()) {
                    ++cacheHitCount;
                }
            }

            Json::Value out;
            out["totalRequests"] = static_cast<int>(requests.size());
            out["allowedRequests"] = allowedCount;
            out["blockedRequests"] = blockedCount;
            out["cacheHits"] = cacheHitCount;
            out["whitelistRules"] = static_cast<int>(adminUI.listWhitelistRules().size());
            out["adminUsers"] = static_cast<int>(adminUI.listAdminUsers().size());
            auto response = drogon::HttpResponse::newHttpJsonResponse(out);
            addCorsHeaders(response);
            logger.info("HTTP GET /admin/stats totalRequests=" + std::to_string(requests.size()));
            callback(response);
        },
        {drogon::Get});

    // Preflight support for browser-based admin UI.
    drogon::app().registerHandler(
        "/admin/whitelist",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP OPTIONS /admin/whitelist");
            callback(corsPreflightResponse());
        },
        {drogon::Options});

    drogon::app().registerHandler(
        "/admin/export/requests",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP OPTIONS /admin/export/requests");
            callback(corsPreflightResponse());
        },
        {drogon::Options});

    drogon::app().registerHandler(
        "/admin/export/requests/download",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP OPTIONS /admin/export/requests/download");
            callback(corsPreflightResponse());
        },
        {drogon::Options});

    drogon::app().registerHandler(
        "/admin/requests",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP OPTIONS /admin/requests");
            callback(corsPreflightResponse());
        },
        {drogon::Options});

    drogon::app().registerHandler(
        "/admin/stats",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP OPTIONS /admin/stats");
            callback(corsPreflightResponse());
        },
        {drogon::Options});
    drogon::app().registerHandler(
        "/admin/cache/stats",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP OPTIONS /admin/cache/stats");
            callback(corsPreflightResponse());
        },
        {drogon::Options});
    drogon::app().registerHandler(
        "/admin/cache/settings",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP OPTIONS /admin/cache/settings");
            callback(corsPreflightResponse());
        },
        {drogon::Options});
    drogon::app().registerHandler(
        "/admin/cache",
        [&logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            logger.info("HTTP OPTIONS /admin/cache");
            callback(corsPreflightResponse());
        },
        {drogon::Options});
    drogon::app().registerHandler(
    "/admin/cache/stats",
    [&cacheService, &logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        logger.info("HTTP GET /admin/cache/stats");
        const auto stats = cacheService.getStats();

        Json::Value out;
        out["defaultTtlSeconds"] = cacheService.getDefaultTtlSeconds();
        out["maxEntries"] = static_cast<Json::UInt64>(cacheService.getMaxEntries());
        out["maxBytes"] = static_cast<Json::UInt64>(cacheService.getMaxBytes());
        out["entries"] = static_cast<Json::UInt64>(stats.entries);
        out["bytes"] = static_cast<Json::UInt64>(stats.bytes);
        out["overflowEvents"] = static_cast<Json::UInt64>(stats.overflowEvents);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
        addCorsHeaders(resp);
        logger.info(
            "HTTP GET /admin/cache/stats entries=" + std::to_string(stats.entries) +
            ", bytes=" + std::to_string(stats.bytes) +
            ", overflowEvents=" + std::to_string(stats.overflowEvents));
        callback(resp);
    },
    {drogon::Get});
    drogon::app().registerHandler(
    "/admin/cache/settings",
    [&cacheService, &logger](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        logger.info("HTTP PATCH /admin/cache/settings");
        const auto json = req->getJsonObject();
        if (!json) {
            logger.warn("HTTP PATCH /admin/cache/settings invalid JSON body");
            callback(jsonError(drogon::k400BadRequest, "JSON body is required"));
            return;
        }

        if (json->isMember("defaultTtlSeconds")) {
            cacheService.setDefaultTtlSeconds((*json)["defaultTtlSeconds"].asInt());
        }

        if (json->isMember("maxEntries") || json->isMember("maxBytes")) {
            const std::size_t maxEntries = json->isMember("maxEntries")
                ? static_cast<std::size_t>((*json)["maxEntries"].asUInt64())
                : cacheService.getMaxEntries();

            const std::size_t maxBytes = json->isMember("maxBytes")
                ? static_cast<std::size_t>((*json)["maxBytes"].asUInt64())
                : cacheService.getMaxBytes();

            cacheService.setLimits(maxEntries, maxBytes);
        }

        Json::Value out;
        out["updated"] = true;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
        addCorsHeaders(resp);
        logger.info("HTTP PATCH /admin/cache/settings updated");
        callback(resp);
    },
    {drogon::Patch});
    drogon::app().registerHandler(
    "/admin/cache",
    [&cacheService, &logger](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        logger.info("HTTP DELETE /admin/cache");
        const std::string url = req->getParameter("url");
        if (url.empty()) {
            logger.warn("HTTP DELETE /admin/cache missing url");
            callback(jsonError(drogon::k400BadRequest, "Query parameter 'url' is required"));
            return;
        }

        const std::size_t removed = cacheService.removeByUrl(url);

        Json::Value out;
        out["removed"] = static_cast<Json::UInt64>(removed);
        out["url"] = url;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
        addCorsHeaders(resp);
        logger.info("HTTP DELETE /admin/cache removed=" + std::to_string(removed) + " for url=" + url);
        callback(resp);
    },
    {drogon::Delete});


        drogon::app().addListener("0.0.0.0", 8080).setThreadNum(4).run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Startup failed: " << ex.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Startup failed: unknown error" << std::endl;
        return 1;
    }
    
}
