// Headless golden-path tests (no verslicer.app required).
// Build: cmake --build build --target ollama_apply_golden_test
// macOS app bundle (manual QA only): build/<arch>/src/Release/verslicer.app

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

static bool catalog_hits_substr(const char* query, const char* key_substr)
{
    const auto hits = OllamaSettingSearch::search(query, 2, 8);
    for (const auto& hit : hits) {
        if (hit.key.find(key_substr) != std::string::npos)
            return true;
    }
    return false;
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
        expect_true(OllamaRequestRouter::classify("베드에 안 붙어") != OllamaRequestRoute::Fast,
                    "adhesion query not fast-route");
    }

    {
        const auto keys = OllamaSettingSearch::candidate_keys_for_request("베드 온도", 2, 5);
        expect_true(!keys.empty(), "catalog keys for explicit bed query");
    }

    {
        const auto hits = OllamaSettingSearch::search("retraction", 2, 5);
        expect_true(!hits.empty(), "retraction catalog search");
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

    {
        const auto hits = OllamaSettingSearch::search("ironing", 2, 5);
        expect_true(!hits.empty(), "rough surface catalog search");
    }

    {
        const auto hits = OllamaSettingSearch::search("nozzle temperature", 2, 5);
        expect_true(!hits.empty(), "nozzle clog catalog search");
    }

    {
        const auto hits = OllamaSettingSearch::search("bridge", 2, 5);
        expect_true(!hits.empty(), "sagging bridge catalog search");
    }

    {
        expect_true(catalog_hits_substr("infill", "infill") || catalog_hits_substr("wall", "wall"),
                    "strong goal catalog keys");
        expect_true(catalog_hits_substr("infill", "infill"), "save material catalog infill");
        expect_true(catalog_hits_substr("layer height", "layer"), "fast goal catalog layer");
        expect_true(catalog_hits_substr("brim", "brim"), "reliable goal catalog brim");
        expect_true(catalog_hits_substr("ironing", "iron") || catalog_hits_substr("layer height", "layer"),
                    "cosmetic goal catalog surface keys");
    }

    {
        expect_true(catalog_hits_substr("retraction", "retraction"), "stringing catalog retraction");
    }

    {
        nlohmann::json blocked = {{"type", "set_config"},
                                  {"preset", "print"},
                                  {"options", {{"machine_start_gcode", "M104"}}}};
        expect_true(!OllamaSettingRegistry::is_allowed_key("machine_start_gcode", "print"),
                    "post-sanitize tier3 key blocked");
        (void) blocked;
    }

    if (g_failures == 0) {
        std::cout << "ollama_apply_golden_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "ollama_apply_golden_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
