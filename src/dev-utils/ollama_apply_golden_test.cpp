#include "../slic3r/GUI/OllamaAssistant/OllamaActionJsonExtract.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaRequestRouter.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaSettingRegistry.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaSettingSearch.hpp"

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

int main()
{
    setenv("OLLAMA_AUTO_CATALOG", "1", 1);

    {
        expect_eq(OllamaRequestRouter::route_name(OllamaRequestRoute::Fast), "fast", "route fast name");
        expect_eq(OllamaRequestRouter::route_name(OllamaRequestRoute::Standard), "standard", "route standard");
        expect_eq(OllamaRequestRouter::route_name(OllamaRequestRoute::Deep), "deep", "route deep");
    }

    {
        expect_true(OllamaRequestRouter::classify("90도 돌려줘") == OllamaRequestRoute::Fast, "rotate -> fast");
        expect_true(OllamaRequestRouter::classify("arrange the plate") == OllamaRequestRoute::Fast, "arrange -> fast");
        expect_true(OllamaRequestRouter::classify("서포트 켜줘") == OllamaRequestRoute::Standard, "support -> standard");
        expect_true(OllamaRequestRouter::classify("고쳐줘") == OllamaRequestRoute::Deep, "vague -> deep");
        expect_true(OllamaRequestRouter::classify("베드에 안 붙어") == OllamaRequestRoute::Standard,
                    "adhesion symptom -> standard");
    }

    {
        const auto keys = OllamaSettingSearch::candidate_keys_for_request("베드에 안 붙", 2, 5);
        expect_true(!keys.empty(), "symptom keys for adhesion");
        bool has_brim = false;
        for (const auto& k : keys)
            if (k.find("brim") != std::string::npos || k == "bed_temperature")
                has_brim = true;
        expect_true(has_brim, "adhesion suggests brim or bed temp");
    }

    {
        const auto keys = OllamaSettingSearch::candidate_keys_for_request("실이 많이 나와", 2, 5);
        expect_true(!keys.empty(), "stringing keys");
    }

    {
        const std::string llama_brim = R"({"message":"현재 brim_width_mm은 \u{0022}5\u{0022}입니다.","actions":[{"type":"set_config","preset":"print","values":{"brim_width":10,"brim_type":"outer_only"}}]})";
        const nlohmann::json root     = extract_ollama_action_json_with_repair(llama_brim);
        expect_true(root.contains("message") && root["message"].is_string(), "es6 unicode json parses");
        expect_true(root.contains("actions") && root["actions"].is_array() && !root["actions"].empty(),
                    "es6 unicode json keeps actions");
        const auto& action = root["actions"][0];
        expect_true(action.value("type", "") == "set_config", "es6 unicode action type");
        expect_true(action.contains("values") && action["values"].is_object(), "llama uses values key");
        expect_true(action["values"].value("brim_width", 0) == 10, "es6 unicode brim_width");
    }

    {
        const std::string broken = R"({"message":"ok","actions":[{"type":"set_config","preset":"print","options":{"layer_height":0.2},])";
        const nlohmann::json root = extract_ollama_action_json_with_repair(broken);
        expect_true(root.contains("actions") && root["actions"].is_array(), "repair trailing comma json");
    }

    {
        const std::string truncated = R"({"message":"ok","actions":[{"type":"set_config","preset":"print","options":{"enable_support":true)";
        const nlohmann::json root = extract_ollama_action_json_with_repair(truncated);
        expect_true(root.contains("actions"), "repair unclosed braces");
    }

    {
        const std::string malformed = R"({" "}
wall_loops: 3,
sparse_infill_density: 10%)";
        const nlohmann::json root = try_salvage_ollama_action_json(malformed);
        expect_true(root.contains("actions") && root["actions"].is_array() && !root["actions"].empty(),
                    "salvage plain-text settings");
        const auto& opts = root["actions"][0]["options"];
        expect_true(opts.value("wall_loops", 0) == 3, "salvage wall_loops");
        expect_true(opts.value("sparse_infill_density", std::string()) == "10%", "salvage infill density");
        const nlohmann::json extracted = extract_ollama_action_json_with_repair(malformed);
        expect_true(extracted.contains("actions") && !extracted["actions"].empty(), "extract falls back to salvage");
    }

    {
        nlohmann::json action = {{"type", "set_config"},
                                 {"preset", "print"},
                                 {"options", {{"layer_height", 0.8}, {"sparse_infill_density", 120}}}};
        for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
            const std::string key = it.key();
            expect_true(OllamaSettingRegistry::is_allowed_key(key, "print"), "golden key allowed");
            OllamaSettingRegistry::clamp_json_value(key, it.value());
        }
        expect_true(action["options"]["layer_height"].get<double>() <= 0.6, "layer_height clamp golden");
        expect_true(action["options"]["sparse_infill_density"].is_string(), "density percent golden");
    }

    {
        expect_true(!OllamaSettingRegistry::is_allowed_key("machine_start_gcode"), "tier3 blocked golden");
    }

    if (g_failures == 0) {
        std::cout << "ollama_apply_golden_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "ollama_apply_golden_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
