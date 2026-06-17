#include "../slic3r/GUI/OllamaAssistant/OllamaActionJsonExtract.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaActionPipelineCore.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaIntentRules.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Slic3r::GUI;
using namespace Slic3r::GUI::OllamaIntentRules;

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
        OllamaActionPipelineCore::dedupe_actions_in_turn(root);
        expect_true(root["actions"].is_array() && root["actions"].size() == 2, "dedupe duplicate set_config");
        expect_true(action_has_type(root, "arrange"), "dedupe keeps arrange");
    }

    {
        const nlohmann::json rotate = {{"type", "rotate"}, {"x", 0.0}, {"y", 0.0}, {"z", 90.0}};
        const std::string  fp1      = OllamaActionPipelineCore::action_fingerprint(rotate);
        const std::string  fp2      = OllamaActionPipelineCore::action_fingerprint(rotate);
        expect_eq(fp1, fp2, "rotate fingerprint stable");
        expect_true(!fp1.empty(), "rotate fingerprint non-empty");
    }

    {
        const std::string text = R"(Here is the plan:
```json
{"message":"ok","actions":[{"type":"arrange"}]}
```)";
        const nlohmann::json root = extract_ollama_action_json(text);
        expect_true(root.contains("actions") && root["actions"].is_array() && root["actions"].size() == 1,
                    "extract fenced json");
        expect_true(action_has_type(root, "arrange"), "extract keeps arrange action");
    }

    {
        const auto z = parse_z_rotation_degrees("모델 90도 돌려줘");
        expect_true(z.has_value(), "parse_z_rotation present");
        if (z)
            expect_true(*z == 90.0, "parse_z_rotation degrees");
    }

    {
        const auto z = parse_z_rotation_degrees("왼쪽으로 45도 회전");
        expect_true(z.has_value() && *z == -45.0, "parse_z_rotation left turn");
    }

    if (g_failures == 0) {
        std::cout << "ollama_pipeline_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "ollama_pipeline_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
