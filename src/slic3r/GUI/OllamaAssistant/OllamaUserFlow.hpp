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

    /** Inject tab/setup/print actions when the user asks for workflow steps. */
    static void ensure_flow_actions_from_user_text(nlohmann::json& root, const std::string& user_request);

    /** Execute a single workflow action (shared by chat executor and coach). */
    static OllamaActionResult apply_flow_action(const nlohmann::json& action, Plater* plater);
};

}} // namespace

#endif
