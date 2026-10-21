// Headless Assistant pipeline tests (no verslicer.app required).
// Build: cmake --build build --target assistant_pipeline_test
// Run:   ctest -R assistant_pipeline_test --output-on-failure
// macOS app bundle (manual QA only): build/<arch>/src/Release/verslicer.app

#include "../slic3r/GUI/Assistant/AssistantActionJsonExtract.hpp"
#include "../slic3r/GUI/Assistant/AssistantActionPipelineCore.hpp"
#include "../slic3r/GUI/Assistant/AssistantAgentGoalPlanner.hpp"
#include "../slic3r/GUI/Assistant/AssistantBenchmarkScenarios.hpp"
#include "../slic3r/GUI/Assistant/AssistantSettingRegistry.hpp"
#include "../slic3r/GUI/Assistant/AssistantSettingSearch.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Slic3r::GUI;

static int g_failures = 0;

static void expect_true(bool cond, const char* label)
{
    if (!cond) {
        std::cerr << "FAIL: " << label << '\n';
        ++g_failures;
    }
}

static void expect_eq(const std::string& got, const std::string& want, const char* label)
{
    if (got != want) {
        std::cerr << "FAIL: " << label << " got='" << got << "' want='" << want << "'\n";
        ++g_failures;
    }
}

static bool action_has_type(const nlohmann::json& root, const char* type)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;
    for (const auto& a : root["actions"]) {
        if (a.is_object() && a.value("type", "") == type)
            return true;
    }
    return false;
}

int main()
{
    setenv("OLLAMA_AUTO_CATALOG", "1", 1);

    {
        nlohmann::json root = {
            {"actions",
             nlohmann::json::array({{{"type", "set_config"},
                                     {"preset", "print"},
                                     {"options", {{"brim_width", 5}}}},
                                    {{"type", "set_config"},
                                     {"preset", "print"},
                                     {"options", {{"brim_width", 5}}}},
                                    {{"type", "arrange"}}})}};
        AssistantActionPipelineCore::dedupe_actions_in_turn(root);
        expect_true(root["actions"].is_array() && root["actions"].size() == 2, "dedupe duplicate set_config");
        expect_true(action_has_type(root, "arrange"), "dedupe keeps arrange");
    }

    {
        const nlohmann::json rotate = {{"type", "rotate"}, {"x", 0.0}, {"y", 0.0}, {"z", 90.0}};
        const std::string  fp1      = AssistantActionPipelineCore::action_fingerprint(rotate);
        const std::string  fp2      = AssistantActionPipelineCore::action_fingerprint(rotate);
        expect_eq(fp1, fp2, "rotate fingerprint stable");
        expect_true(!fp1.empty(), "rotate fingerprint non-empty");
    }

    {
        const std::string text = R"(Here is the plan:
```json
{"message":"ok","actions":[{"type":"arrange"}]}
```)";
        const nlohmann::json root = extract_assistant_action_json(text);
        expect_true(root.contains("actions") && root["actions"].is_array() && root["actions"].size() == 1,
                    "extract fenced json");
        expect_true(action_has_type(root, "arrange"), "extract keeps arrange action");
    }

    {
        const auto& scenarios = assistant_benchmark_scenarios();
        expect_true(scenarios.size() >= 25, "benchmark scenarios wired");
        for (const auto& sc : scenarios) {
            if (!sc.check(sc.user_request))
                std::cerr << "FAIL scenario: " << sc.id << '\n';
            expect_true(sc.check(sc.user_request), sc.id);
        }
    }

    {
        const auto keys = AssistantSettingSearch::keys_from_planner_json(
            R"({"intent":"x","candidate_keys":["layer_height"],"message":"ok"})");
        expect_true(keys.size() == 1 && keys[0] == "layer_height", "planner keys parse");
    }

    {
        const std::string repaired = repair_assistant_json_text(R"({"a":1,})");
        expect_true(repaired.find(",}") == std::string::npos, "repair strips trailing comma");
    }

    {
        const auto keys = AssistantSettingSearch::candidate_keys_for_request("increase infill", 2, 5);
        expect_true(!keys.empty(), "infill candidate keys");
    }

    {
        const nlohmann::json hint = AssistantAgentGoalPlanner::build_plan_hint("turn on supports", false);
        expect_true(hint.contains("hint") && !hint.contains("candidate_keys"), "plan hint has no keyword keys");
        expect_true(!AssistantAgentGoalPlanner::should_auto_finish_after_apply("turn on supports", false, {}),
                    "apply does not end the loop");
    }

    {
        expect_true(!AssistantSettingRegistry::is_allowed_key("machine_start_gcode", "print"),
                    "tier3 key blocked for sanitize strip");
    }

    if (g_failures == 0) {
        std::cout << "ollama_pipeline_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "ollama_pipeline_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
