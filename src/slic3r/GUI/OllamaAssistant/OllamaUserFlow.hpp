#ifndef slic3r_OllamaUserFlow_hpp_
#define slic3r_OllamaUserFlow_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

class Plater;

struct OllamaActionResult;

struct OllamaFlowDispatchResult
{
    bool        handled{ false };
    bool        setup_blocked{ false };
    std::string blocked_message;
};

/** Unified user-journey context for LLM + coach (setup steps, tab, next action). */
class OllamaUserFlow
{
public:
    static nlohmann::json build_flow_context_json();
    static std::string    flow_prompt_block(bool korean);

    /** Coach / overlay buttons → same action pipeline as Ollama chat. */
    static OllamaFlowDispatchResult dispatch_coach_action(const std::string& action_id, Plater* plater);

    /**
     * Deterministic acquisition-intent gate ("get me X and print it").
     * True when the utterance combines a print/acquisition verb (출력해줘,
     * 뽑아줘, print me, I want to print, …) with an object noun phrase, and is
     * NOT a config request (인필/브림/infill/…), NOT a symptom report
     * (안 붙어/들뜸/won't stick/…), NOT a question, and NOT a reference to the
     * model already on the plate. Pure function of its inputs (unit-testable).
     */
    static bool is_acquisition_print_request(const std::string& user_utf8, bool plate_has_model);

    /** Drop Smart Print tab navigation when the assistant already proposes set_config fixes. */
    static void prune_navigation_for_config_fixes(nlohmann::json& root, const std::string& user_request);

    /** True when an action result only switched UI (tab/panel), not print settings. */
    static bool result_is_navigation_only(const OllamaActionResult& result);

    /** Execute a single workflow action (shared by chat executor and coach). */
    static OllamaActionResult apply_flow_action(const nlohmann::json& action, Plater* plater);
};

}} // namespace

#endif
