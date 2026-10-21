// Grades recorded assistant replies against golden cases.
//
// Always: unit-checks the grader with synthetic replies.
// With ASSISTANT_EVAL_LOG=/path/to/log.jsonl (written by the app when
// VERSLICER_EVAL_LOG is set), also prints accuracy per model. Low scores are
// reported, not failed, unless ASSISTANT_EVAL_MIN_PASS (0-100) is set.

#include "../slic3r/GUI/Assistant/AssistantEvalGrader.hpp"
#include "../slic3r/GUI/Assistant/AssistantActionJsonExtract.hpp"
#include "../slic3r/GUI/Assistant/AssistantFailureDoctor.hpp"
#include "../slic3r/GUI/Assistant/AssistantPrinterControl.hpp"
#include "../slic3r/GUI/Assistant/AssistantActionRegistry.hpp"
#include "../slic3r/GUI/Assistant/AssistantSettingRegistry.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#ifndef ASSISTANT_EVAL_CASES
#error "ASSISTANT_EVAL_CASES must point at assistant_cases.json"
#endif

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
    std::ifstream cases_file(ASSISTANT_EVAL_CASES);
    nlohmann::json doc;
    try {
        cases_file >> doc;
    } catch (const std::exception& e) {
        std::cerr << "cannot read " << ASSISTANT_EVAL_CASES << ": " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    const auto cases = AssistantEvalGrader::parse_cases(doc);
    expect_true(cases.size() >= 50, "golden case count");
    // A case must only ask for answers the assistant is allowed to give.
    for (const auto& c : cases) {
        for (const auto& k : c.expect_keys)
            if (!AssistantSettingRegistry::is_allowed_key(k)) {
                std::cerr << "FAIL: case " << c.id << " expects key the registry blocks: " << k << '\n';
                ++g_failures;
            }
        for (const auto& t : c.expect_actions)
            if (!AssistantActionRegistry::is_allowed_type(t)) {
                std::cerr << "FAIL: case " << c.id << " expects unknown action: " << t << '\n';
                ++g_failures;
            }
    }

    AssistantEvalCase infill{"t", "infill", {"set_config"}, {"sparse_infill_density"}, {"send_print"}};
    expect_true(AssistantEvalGrader::grade(infill, R"({"message":"ok","actions":[{"type":"set_config","preset":"print","options":{"sparse_infill_density":"30%"}}]})").pass,
                "grade pass");
    expect_true(!AssistantEvalGrader::grade(infill, R"({"message":"ok","actions":[{"type":"set_config","options":{"wall_loops":4}}]})").pass,
                "grade missing key");
    expect_true(!AssistantEvalGrader::grade(infill, R"({"message":"ok","actions":[{"type":"set_config","options":{"sparse_infill_density":"30%"}},{"type":"send_print"}]})").pass,
                "grade forbidden action");
    expect_true(!AssistantEvalGrader::grade(infill, "I changed the infill for you.").pass, "grade prose only");

    AssistantEvalCase brim{"b", "brim", {}, {}, {}};
    AssistantEvalCase no_brim{"nb", "no brim", {}, {}, {}};
    const std::vector<AssistantEvalCase> overlap{brim, no_brim};
    const auto* hit = AssistantEvalGrader::match_case(overlap, "context... user: no brim please");
    expect_true(hit && hit->id == "nb", "match prefers longest request");

    const auto scores = AssistantEvalGrader::score_log(cases, {
        {{"model", "gpt-a"}, {"user", "Rotate it 45 degrees"}, {"reply", R"({"actions":[{"type":"rotate","z":45}]})"}},
        {{"model", "gpt-a"}, {"user", "Turn on supports"}, {"reply", R"({"actions":[]})"}},
        {{"model", "gpt-b"}, {"user", "Rotate it 45 degrees"}, {"reply", R"({"actions":[{"type":"set_config","options":{}}]})"}},
    });
    expect_true(scores.at("gpt-a").total == 2 && scores.at("gpt-a").passed == 1, "score_log gpt-a");
    expect_true(scores.at("gpt-b").total == 1 && scores.at("gpt-b").passed == 0, "score_log gpt-b");

    // Salvaging prose must not throw on number-like junk ("-", huge integers).
    for (const char* prose : {"layer_height: -\n", "wall_loops: 99999999999999\n", "layer_height = .\n"}) {
        try {
            (void) try_salvage_assistant_action_json(prose);
        } catch (const std::exception& e) {
            std::cerr << "FAIL: salvage threw on '" << prose << "': " << e.what() << '\n';
            ++g_failures;
        }
    }

    // Removed capabilities must stay removed: the AI cannot send or model.
    for (const char* gone : {"send_print", "mesh_boolean"})
        expect_true(!AssistantActionRegistry::is_allowed_type(gone), gone);

    // Printer control: only four targets, only within the printer's limits.
    {
        PrinterControlLimits lim;
        lim.online = true;
        lim.nozzle_min = 170; lim.nozzle_max = 300; lim.bed_max = 110;
        lim.chamber_editable = true; lim.chamber_max = 60; lim.chamber_heat_switch = 40;
        auto ok = [&](const char* json_text) {
            std::string err;
            return AssistantPrinterControl::validate(nlohmann::json::parse(json_text), lim, &err).has_value();
        };
        expect_true(ok(R"({"type":"printer_control","target":"nozzle_temp","value":215})"), "pc nozzle 215 ok");
        expect_true(ok(R"({"type":"printer_control","target":"nozzle_temp","value":"220"})"), "pc nozzle string ok");
        expect_true(ok(R"({"type":"printer_control","target":"nozzle_temp","value":0})"), "pc nozzle off ok");
        expect_true(!ok(R"({"type":"printer_control","target":"nozzle_temp","value":350})"), "pc nozzle over max");
        expect_true(!ok(R"({"type":"printer_control","target":"nozzle_temp","value":100})"), "pc nozzle under min");
        expect_true(!ok(R"({"type":"printer_control","target":"nozzle_temp","value":"hot"})"), "pc nozzle not a number");
        expect_true(!ok(R"({"type":"printer_control","target":"nozzle_temp"})"), "pc nozzle missing value");
        expect_true(ok(R"({"type":"printer_control","target":"bed_temp","value":60})"), "pc bed 60 ok");
        expect_true(!ok(R"({"type":"printer_control","target":"bed_temp","value":130})"), "pc bed over max");
        expect_true(!ok(R"({"type":"printer_control","target":"bed_temp","value":-5})"), "pc bed negative");
        expect_true(ok(R"({"type":"printer_control","target":"chamber_temp","value":35})"), "pc chamber 35 ok");
        expect_true(!ok(R"({"type":"printer_control","target":"chamber_temp","value":45})"), "pc chamber heat switch needs user");
        expect_true(!ok(R"({"type":"printer_control","target":"print_speed","value":"sport"})"), "pc speed needs printing");
        expect_true(!ok(R"({"type":"printer_control","target":"fan","value":100})"), "pc unknown target");
        expect_true(!ok(R"({"type":"printer_control","target":"start_print"})"), "pc no start");
        lim.printing = true;
        expect_true(ok(R"({"type":"printer_control","target":"print_speed","value":"sport"})"), "pc speed sport");
        expect_true(ok(R"({"type":"printer_control","target":"print_speed","value":4})"), "pc speed 4");
        expect_true(!ok(R"({"type":"printer_control","target":"print_speed","value":9})"), "pc speed 9");
        lim.chamber_cooling_while_printing = true;
        expect_true(!ok(R"({"type":"printer_control","target":"chamber_temp","value":30})"), "pc chamber cooling mode");
        lim.chamber_editable = false;
        expect_true(!ok(R"({"type":"printer_control","target":"chamber_temp","value":30})"), "pc chamber not editable");
        lim.online = false;
        expect_true(!ok(R"({"type":"printer_control","target":"bed_temp","value":60})"), "pc offline");
        expect_true(AssistantActionRegistry::is_allowed_type("printer_control"), "pc registered");
    }

    // Failure doctor: error codes map to the module that raised them.
    expect_true(AssistantFailureDoctor::area_for_code("0700200000020001").area == "ams_feeding", "doctor AMS HMS code");
    expect_true(AssistantFailureDoctor::area_for_code("0300-8001").area == "motion_first_layer", "doctor MC print_error");
    expect_true(AssistantFailureDoctor::area_for_code("0c00-8005").area == "camera_detection", "doctor lowercase xcam");
    expect_true(AssistantFailureDoctor::area_for_code("0500-4001").setting_keys.empty(), "doctor mainboard has no settings");
    expect_true(AssistantFailureDoctor::area_for_code("").area == "unknown", "doctor empty code");
    expect_true(AssistantFailureDoctor::printer_diagnostics_json().is_object(), "doctor headless json");

    if (const char* log_path = std::getenv("ASSISTANT_EVAL_LOG")) {
        std::ifstream log(log_path);
        std::vector<nlohmann::json> records;
        for (std::string line; std::getline(log, line);) {
            try {
                records.push_back(nlohmann::json::parse(line));
            } catch (...) {
            }
        }
        const int min_pass = std::getenv("ASSISTANT_EVAL_MIN_PASS") ? std::atoi(std::getenv("ASSISTANT_EVAL_MIN_PASS")) : -1;
        std::cout << "model accuracy over " << cases.size() << " golden cases (" << records.size() << " log records)\n";
        for (const auto& [model, s] : AssistantEvalGrader::score_log(cases, records)) {
            const int pct = s.total ? s.passed * 100 / s.total : 0;
            std::cout << "  " << std::left << std::setw(24) << model << s.passed << "/" << s.total << "  (" << pct << "%)\n";
            for (const auto& [id, reason] : s.failures)
                std::cout << "      - " << id << ": " << reason << '\n';
            if (min_pass >= 0 && pct < min_pass) {
                std::cerr << "FAIL: " << model << " below ASSISTANT_EVAL_MIN_PASS=" << min_pass << '\n';
                ++g_failures;
            }
        }
    }

    if (g_failures == 0) {
        std::cout << "assistant_eval_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "assistant_eval_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
