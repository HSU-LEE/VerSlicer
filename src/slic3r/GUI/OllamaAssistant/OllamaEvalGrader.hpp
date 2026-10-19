#ifndef slic3r_OllamaEvalGrader_hpp_
#define slic3r_OllamaEvalGrader_hpp_

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** One golden case: what the user says and what a correct reply must contain. */
struct OllamaEvalCase
{
    std::string              id;
    std::string              request;
    std::vector<std::string> expect_actions;  // every listed type must appear
    std::vector<std::string> expect_keys;     // every listed set_config option key must appear
    std::vector<std::string> forbid_actions;  // none of these types may appear
};

struct OllamaEvalVerdict
{
    bool        pass{false};
    std::string reason; // empty on pass
};

/** Per-model tally over a set of graded replies. */
struct OllamaEvalScore
{
    int                                passed{0};
    int                                total{0};
    std::map<std::string, std::string> failures; // case id -> reason
};

/** Offline grader for recorded assistant replies (no GUI or network). */
class OllamaEvalGrader
{
public:
    /** Parse cases from {"cases":[{id, request, expect_actions, expect_keys, forbid_actions}]}. */
    static std::vector<OllamaEvalCase> parse_cases(const nlohmann::json& doc);

    static OllamaEvalVerdict grade(const OllamaEvalCase& c, const std::string& reply_text);

    /** Find the case whose request appears in a recorded user message. */
    static const OllamaEvalCase* match_case(const std::vector<OllamaEvalCase>& cases, const std::string& user_message);

    /** Score JSONL records {"model", "user", "reply"} from VERSLICER_EVAL_LOG, grouped by model.
     *  Records that match no case are ignored; the last reply per (model, case) wins. */
    static std::map<std::string, OllamaEvalScore> score_log(const std::vector<OllamaEvalCase>& cases,
                                                            const std::vector<nlohmann::json>& records);
};

}} // namespace

#endif
