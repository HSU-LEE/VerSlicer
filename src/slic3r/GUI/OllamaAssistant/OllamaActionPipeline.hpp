#ifndef slic3r_OllamaActionPipeline_hpp_
#define slic3r_OllamaActionPipeline_hpp_

#include "OllamaActionValidator.hpp"
#include "OllamaResponseNormalizer.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI {

struct OllamaPipelineOptions
{
    bool        apply_mode{true};
    bool        include_makerworld{true};
    bool        advisor_filter{false};
    bool        question_mode_strip{false};
    std::string user_request;
};

struct OllamaPipelineResult
{
    OllamaNormalizeResult     normalized;
    OllamaActionSanitizeResult sanitized;
    bool                      actions_empty{true};
};

/** normalize → sanitize → dedupe (and optional advisor filter). */
class OllamaActionPipeline
{
public:
    static OllamaPipelineResult process_actions(nlohmann::json& root, const OllamaPipelineOptions& opt);

    /** Rule-only recovery when LLM JSON parse fails. */
    static nlohmann::json build_rule_only_root(const std::string& user_request, bool include_makerworld = true);

    static nlohmann::json extract_from_assistant_text(const std::string& assistant_text);

    static void dedupe_actions_in_turn(nlohmann::json& root);

    static void strip_actions_for_question_history(nlohmann::json& root);

    /** Same normalize/sanitize/dedupe path as apply; returns false if no executable actions remain. */
    static bool prepare_apply_root(nlohmann::json& root, const std::string& user_request,
                                   bool include_makerworld = false);
};

}} // namespace

#endif
