// Grades recorded assistant replies against golden cases.
//
// Always: unit-checks the grader with synthetic replies.
// With ASSISTANT_EVAL_LOG=/path/to/log.jsonl (written by the app when
// VERSLICER_EVAL_LOG is set), also prints accuracy per model. Low scores are
// reported, not failed, unless ASSISTANT_EVAL_MIN_PASS (0-100) is set.

#include "../slic3r/GUI/OllamaAssistant/OllamaEvalGrader.hpp"

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
    const auto cases = OllamaEvalGrader::parse_cases(doc);
    expect_true(cases.size() >= 15, "golden case count");

    OllamaEvalCase infill{"t", "infill", {"set_config"}, {"sparse_infill_density"}, {"send_print"}};
    expect_true(OllamaEvalGrader::grade(infill, R"({"message":"ok","actions":[{"type":"set_config","preset":"print","options":{"sparse_infill_density":"30%"}}]})").pass,
                "grade pass");
    expect_true(!OllamaEvalGrader::grade(infill, R"({"message":"ok","actions":[{"type":"set_config","options":{"wall_loops":4}}]})").pass,
                "grade missing key");
    expect_true(!OllamaEvalGrader::grade(infill, R"({"message":"ok","actions":[{"type":"set_config","options":{"sparse_infill_density":"30%"}},{"type":"send_print"}]})").pass,
                "grade forbidden action");
    expect_true(!OllamaEvalGrader::grade(infill, "I changed the infill for you.").pass, "grade prose only");

    OllamaEvalCase brim{"b", "brim", {}, {}, {}};
    OllamaEvalCase no_brim{"nb", "no brim", {}, {}, {}};
    const std::vector<OllamaEvalCase> overlap{brim, no_brim};
    const auto* hit = OllamaEvalGrader::match_case(overlap, "context... user: no brim please");
    expect_true(hit && hit->id == "nb", "match prefers longest request");

    const auto scores = OllamaEvalGrader::score_log(cases, {
        {{"model", "gpt-a"}, {"user", "Rotate it 45 degrees"}, {"reply", R"({"actions":[{"type":"rotate","z":45}]})"}},
        {{"model", "gpt-a"}, {"user", "Turn on supports"}, {"reply", R"({"actions":[]})"}},
        {{"model", "gpt-b"}, {"user", "Rotate it 45 degrees"}, {"reply", R"({"actions":[{"type":"set_config","options":{}}]})"}},
    });
    expect_true(scores.at("gpt-a").total == 2 && scores.at("gpt-a").passed == 1, "score_log gpt-a");
    expect_true(scores.at("gpt-b").total == 1 && scores.at("gpt-b").passed == 0, "score_log gpt-b");

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
        for (const auto& [model, s] : OllamaEvalGrader::score_log(cases, records)) {
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
