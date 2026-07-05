#ifndef slic3r_OllamaContextBuilder_hpp_
#define slic3r_OllamaContextBuilder_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/**
 * Per-turn slicer context JSON for the LLM (moved out of OllamaActionExecutor).
 * Owns the signature-keyed context cache; all builders must run on the wx main
 * thread (they read Plater / preset bundle / menu state).
 */
class OllamaContextBuilder
{
public:
    static std::string build_context_json();
    /** Small context for the first chat turn (avoids overloading the model). */
    static std::string build_compact_context_json();

    /** Trim context JSON structurally (drop low-priority catalog entries) before sending to the model. */
    static std::string fit_context_json_to_limit(std::string json, size_t max_chars);

    /** Clear cached LLM context after settings change. */
    static void invalidate_context_cache();

    /** Invalidate context cache and refresh intent signals; optionally clear coach dedup. */
    static void notify_plater_context_changed(bool clear_coach_dedup = false);

    /** True when the UI language is Korean (used to localize context strings). */
    static bool ui_prefers_korean();

    /** True for calibration menus (excluded from the menu catalog / AI control). */
    static bool is_calibration_menu_name(const std::string& name);
};

}} // namespace

#endif
