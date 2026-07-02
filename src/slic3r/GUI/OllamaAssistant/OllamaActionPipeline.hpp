#ifndef slic3r_OllamaActionPipeline_hpp_
#define slic3r_OllamaActionPipeline_hpp_

#include "OllamaActionValidator.hpp"
#include "OllamaExecutionPolicy.hpp"
#include "OllamaResponseNormalizer.hpp"

#include "OllamaActionExecutor.hpp"

#include <wx/window.h>

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI {

struct OllamaPipelineOptions
{
    bool        apply_mode{true};
    bool        include_makerworld{true};
    bool        advisor_filter{false};
    bool        question_mode_strip{false};
    /** When true, skip mesh action reconciliation (assist loop after first apply). */
    bool        mutations_already_applied{false};
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

    static nlohmann::json build_symptom_fallback_root(const std::string& user_request,
                                                      bool include_makerworld = true);

    /** Salvage JSON from assistant text and normalize (no rule-based action injection). */
    static nlohmann::json build_recovery_root(const std::string& assistant_text, const std::string& user_request,
                                              bool include_makerworld = true);

    static nlohmann::json extract_from_assistant_text(const std::string& assistant_text);

    static void dedupe_actions_in_turn(nlohmann::json& root);

    static void strip_actions_for_question_history(nlohmann::json& root);

    /** Same normalize/sanitize/dedupe path as apply; returns false if no executable actions remain. */
    static bool prepare_apply_root(nlohmann::json& root, const std::string& user_request,
                                   bool include_makerworld = false);

    /** Intents safe to apply without LLM when rule_only_fallback is enabled. */
    static bool high_confidence_rule_only_intent(const std::string& user_request);

    /** Build and validate a rule-only apply root (no LLM). */
    static nlohmann::json build_rule_only_apply_root(const std::string& user_request,
                                                     bool include_makerworld = true);
};

}} // namespace

#endif
