#ifndef slic3r_OllamaResponseNormalizer_hpp_
#define slic3r_OllamaResponseNormalizer_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct OllamaNormalizeResult
{
    std::vector<std::string> warnings;
};

enum class OllamaRuleFallbackScope
{
    AllEligible,
    HighConfidenceOnly,
};

class OllamaResponseNormalizer
{
public:
    /** Patch/coerce LLM JSON using user intent heuristics (shared by chat and model-load advisor). */
    static OllamaNormalizeResult normalize(nlohmann::json& root, const std::string& user_request,
                                           bool include_makerworld = true,
                                           bool force_user_intent  = false);
    static void drop_redundant_slice_actions(nlohmann::json& root);

    /** True when actions contain set_config with at least one allowed option key. */
    static bool has_viable_set_config(const nlohmann::json& root);

    /** Drop stringing misreads and ensure print-speed options when the user wants faster printing. */
    static void reconcile_speed_intent_actions(nlohmann::json& root, const std::string& user_request);

    /** Inject set_config fallbacks from symptom / use-case / slice signals. */
    static void inject_rule_fallbacks(nlohmann::json& root, const std::string& user_request,
                                    OllamaRuleFallbackScope scope = OllamaRuleFallbackScope::AllEligible);
};

}} // namespace

#endif
