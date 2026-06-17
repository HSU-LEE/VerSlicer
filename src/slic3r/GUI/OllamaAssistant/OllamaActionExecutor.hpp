#ifndef slic3r_OllamaActionExecutor_hpp_
#define slic3r_OllamaActionExecutor_hpp_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { class DynamicPrintConfig; }

namespace Slic3r { namespace GUI {

struct OllamaActionResult
{
    bool        success{false};
    bool        effective_change{false};
    std::string message;
};

struct OllamaSetConfigDryRunResult
{
    bool        ok{false};
    int         attempted{0};
    int         changed{0};
    std::string errors;
};

class OllamaActionExecutor
{
public:
    /** @param apply_mode false = question-only chat (no slicer actions). */
    static std::string build_system_prompt(bool apply_mode = true);
    static std::string build_context_json();
    /** Small context for the first chat turn (avoids overloading the model). */
    static std::string build_compact_context_json();

    /** Trim context JSON structurally (drop low-priority catalog entries) before sending to the model. */
    static std::string fit_context_json_to_limit(std::string json, size_t max_chars);

    /** Extract JSON object from assistant text (fenced block or raw). */
    static nlohmann::json extract_action_json(const std::string& assistant_text);

    /** Normalize set_config options (aliases, enable_brim, etc.) before apply or preview. */
    static void normalize_set_config_options(nlohmann::json& options);

    /** Normalize a single config key (aliases). */
    static std::string normalize_config_key(const std::string& key);

    /** Validate set_config against a preset without mutating live state. */
    static OllamaSetConfigDryRunResult dry_run_set_config(const nlohmann::json& action);

    /** Apply all set_config actions in root onto a config snapshot (preview/simulate). */
    static void apply_set_config_actions_to_config(DynamicPrintConfig& cfg, const nlohmann::json& root);

    /** Clear cached LLM context after settings change. */
    static void invalidate_context_cache();

    /** Invalidate context cache and refresh intent signals; optionally clear coach dedup. */
    static void notify_plater_context_changed(bool clear_coach_dedup = false);

    /** Fill/coerce set_config values using live preset + relative phrases in user text. */
    static void augment_actions_from_user_text(nlohmann::json& root, const std::string& user_request);

    static std::vector<OllamaActionResult> execute(const nlohmann::json& root);
};

}} // namespace

#endif
