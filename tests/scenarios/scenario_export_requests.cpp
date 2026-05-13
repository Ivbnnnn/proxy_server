#include "TestSupport.h"

#include "proxy/AdminUI.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>

int main() {
    try {
        proxy::AppLogger logger("scenario-export-requests", proxy::LogLevel::ERROR, std::nullopt, false);
        auto db = proxy::test::createCleanDb(logger);
        proxy::AdminUI admin(*db, logger);

        proxy::RequestRecord first;
        first.setUrl("http://export.scenario/one");
        first.setMethod("GET");
        first.setClientIp("127.0.0.1");
        first.setStatusCode(200);
        first.setAllowed(true);
        first.setCacheHit(false);
        first.setResponseTimeMs(10);
        db->saveRequest(first);

        proxy::RequestRecord second;
        second.setUrl("http://export.scenario/two,with,comma");
        second.setMethod("POST");
        second.setClientIp("127.0.0.2");
        second.setStatusCode(201);
        second.setAllowed(true);
        second.setCacheHit(true);
        second.setResponseTimeMs(20);
        db->saveRequest(second);

        const auto path = std::filesystem::temp_directory_path() / "proxy_scenario_requests_export.csv";
        std::filesystem::remove(path);
        admin.exportRequestsCsv(path.string());

        std::ifstream in(path);
        proxy::test::requireScenario(in.is_open(), "CSV export file is created");
        const std::string csv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        proxy::test::requireScenario(csv.find("id;url;method;clientIp;statusCode;allowed;cacheHit;matchedRuleId;responseTimeMs;requestedAt") != std::string::npos, "CSV header is present");
        proxy::test::requireScenario(csv.find("http://export.scenario/one") != std::string::npos, "first request is exported");
        proxy::test::requireScenario(csv.find("http://export.scenario/two,with,comma") != std::string::npos, "comma-containing URL is exported");

        std::filesystem::remove(path);
        std::cout << "scenario_export_requests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
