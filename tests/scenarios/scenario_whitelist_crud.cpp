#include "TestSupport.h"

#include "proxy/AdminUI.h"
#include "proxy/FilterService.h"

#include <exception>
#include <iostream>
#include <optional>

int main() {
    try {
        proxy::AppLogger logger("scenario-whitelist-crud", proxy::LogLevel::ERROR, std::nullopt, false);
        auto db = proxy::test::createCleanDb(logger);
        proxy::AdminUI admin(*db, logger);

        admin.createWhitelistRule(proxy::test::whitelistRule("http://scenario.test/api", proxy::MatchType::PREFIX, true, "create"));
        auto rules = admin.listWhitelistRules();
        proxy::test::requireScenario(rules.size() == 1, "created whitelist rule is visible");

        proxy::FilterService filter(*db, logger);
        proxy::test::requireScenario(filter.isAllowed("http://scenario.test/api/users"), "created rule allows prefixed URL");

        rules[0].setUrlPattern("http://scenario.test/v2");
        rules[0].setComment("updated");
        admin.updateWhitelistRule(rules[0]);
        filter.reloadRules();
        proxy::test::requireScenario(!filter.isAllowed("http://scenario.test/api/users"), "old prefix is blocked after update");
        proxy::test::requireScenario(filter.isAllowed("http://scenario.test/v2/users"), "new prefix is allowed after update");

        admin.deleteWhitelistRule(rules[0].getId());
        filter.reloadRules();
        proxy::test::requireScenario(!filter.isAllowed("http://scenario.test/v2/users"), "deleted rule blocks URL");

        std::cout << "scenario_whitelist_crud passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
