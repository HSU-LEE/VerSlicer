#include "../slic3r/GUI/Assistant/AssistantBenchmarkScenarios.hpp"
#include "../slic3r/GUI/Assistant/AssistantConfig.hpp"
#include "../slic3r/GUI/Assistant/AssistantSettingCatalogBuilder.hpp"
#include "../slic3r/GUI/Assistant/AssistantSettingRegistry.hpp"
#include "../slic3r/GUI/Assistant/AssistantSettingSearch.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>

using namespace Slic3r::GUI;

static int g_failures = 0;

static void expect_true(bool cond, const char* label)
{
    if (!cond) {
        std::cerr << "FAIL: " << label << '\n';
        ++g_failures;
    }
}

int main()
{
    setenv("OLLAMA_AUTO_CATALOG", "1", 1);

    const auto& scenarios = assistant_benchmark_scenarios();
    expect_true(scenarios.size() >= 25, "benchmark scenario count");
    for (const auto& sc : scenarios) {
        if (!sc.check(sc.user_request))
            std::cerr << "FAIL scenario: " << sc.id << " request='" << sc.user_request << "'\n";
        expect_true(sc.check(sc.user_request), sc.id);
    }

    const auto& catalog = AssistantSettingCatalogBuilder::all();
    expect_true(catalog.size() >= 80, "auto catalog tier1+ size");

    size_t tier1 = 0, tier2 = 0, tier3 = 0;
    for (const auto& sp : catalog) {
        switch (sp.tier) {
        case AssistantSettingTier::Simple: ++tier1; break;
        case AssistantSettingTier::Advanced: ++tier2; break;
        case AssistantSettingTier::Restricted: ++tier3; break;
        }
    }
    expect_true(tier1 >= 50, "tier1 simple keys");
    expect_true(tier2 >= 50, "tier2 advanced keys");
    expect_true(tier3 >= 5, "tier3 restricted keys");

    expect_true(AssistantSettingCatalogBuilder::is_restricted_key("machine_start_gcode"), "tier3 gcode block");
    expect_true(!AssistantSettingRegistry::is_allowed_key("machine_start_gcode"), "registry blocks gcode");

    const nlohmann::json index = AssistantSettingRegistry::build_setting_index(2);
    expect_true(index.is_array() && index.size() >= 80, "setting_index packed");

    const auto hits = AssistantSettingSearch::search("layer height", 2, 5);
    expect_true(!hits.empty(), "search layer height");

    expect_true(assistant_auto_catalog_enabled(), "auto catalog default on");
    if (g_failures == 0) {
        std::cout << "ollama_benchmark_test: " << scenarios.size() << " scenarios, catalog=" << catalog.size()
                  << " (tier1=" << tier1 << " tier2=" << tier2 << " tier3=" << tier3 << ")\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "ollama_benchmark_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
