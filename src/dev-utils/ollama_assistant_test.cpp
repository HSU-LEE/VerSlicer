#include "../slic3r/GUI/OllamaAssistant/OllamaActionRegistry.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaConfig.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaIntentContext.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaIntentRules.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaModelPick.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaSettingRegistry.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaSettingCatalogBuilder.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

    expect_true(OllamaSettingRegistry::is_allowed_key("brim_width"), "allowed brim_width");
    expect_true(OllamaSettingRegistry::is_allowed_key("enable_support"), "allowed enable_support");
    expect_true(!OllamaSettingRegistry::is_allowed_key("machine_start_gcode"), "block tier3 gcode");
    expect_true(OllamaSettingRegistry::is_allowed_key("brim_width", "print"), "brim_width print scope");
    expect_true(!OllamaSettingRegistry::is_allowed_key("brim_width", "filament"), "brim_width not filament");
    expect_true(OllamaSettingRegistry::is_allowed_key("retraction_length", "filament"), "retraction filament");
    expect_true(!OllamaSettingRegistry::is_allowed_key("retraction_length", "print"), "retraction not print");
    expect_true(OllamaSettingRegistry::is_virtual_key("enable_brim"), "enable_brim virtual");
    expect_true(OllamaSettingCatalogBuilder::all().size() >= 80, "auto catalog size");

    nlohmann::json layer = 0.8;
    expect_true(OllamaSettingRegistry::clamp_json_value("layer_height", layer), "clamp layer_height high");
    expect_true(layer.get<double>() <= 0.6, "layer_height clamped max");

    nlohmann::json density = 120;
    OllamaSettingRegistry::clamp_json_value("sparse_infill_density", density);
    expect_true(density.is_string() && density.get<std::string>().find('%') != std::string::npos,
                "density clamp to percent string");

    nlohmann::json temp = 400;
    OllamaSettingRegistry::clamp_json_value("nozzle_temperature", temp);
    expect_true(temp.is_number_integer() && temp.get<int>() <= 300, "nozzle temp clamped");

    const nlohmann::json priority = OllamaSettingRegistry::build_priority_catalog(nullptr, false, 5);
    expect_true(priority.is_array() && priority.size() == 5, "priority catalog top 5");

    expect_true(OllamaActionRegistry::is_allowed_in_advisor("set_config"), "advisor set_config");
    expect_true(!OllamaActionRegistry::is_allowed_in_advisor("scale"), "advisor blocks scale");

    const std::vector<std::string> models = {"qwen2.5:3b", "mistral:latest"};
    expect_eq(pick_installed_ollama_model(models, "unknown"), "qwen2.5:3b", "pick fallback first");
    expect_eq(normalize_ollama_model_tag("qwen2.5"), "qwen2.5:3b", "normalize model tag");
    expect_eq(normalize_ollama_model_tag("qwen2.5:7b"), "qwen2.5:3b", "migrate 7b to 3b");

    expect_true(ollama_recommended_brim_width_mm(15.0, 15.0) == 8.0, "small footprint wide brim");
    expect_true(ollama_recommended_brim_width_mm(50.0, 50.0) == 5.0, "medium footprint default brim");
    expect_true(ollama_recommended_brim_width_mm(100.0, 100.0) == 3.0, "large footprint narrow brim");

    expect_true(ollama_selection_is_tall_narrow(10.0, 10.0, 35.0), "tall narrow ratio");
    expect_true(!ollama_selection_is_tall_narrow(50.0, 50.0, 80.0), "not tall narrow");

    expect_true(OllamaIntentContext::clamp_filament_index(-1, 4) == 0, "filament idx clamp low");
    expect_true(OllamaIntentContext::clamp_filament_index(9, 4) == 3, "filament idx clamp high");

    {
        const auto z = OllamaIntentRules::parse_z_rotation_degrees("90도 돌려");
        expect_true(z.has_value() && *z == 90.0, "parse rotation degrees");
    }

    if (g_failures == 0) {
        std::cout << "ollama_assistant_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "ollama_assistant_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
