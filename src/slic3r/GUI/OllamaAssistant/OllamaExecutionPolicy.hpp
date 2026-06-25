#ifndef slic3r_OllamaExecutionPolicy_hpp_
#define slic3r_OllamaExecutionPolicy_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** How AI actions pass through confirmation dialogs and inline execution. */
enum class OllamaExecutionPolicy
{
    ConfirmAlways, /** Always show review dialog for set_config. */
    AutoSafe,      /** Geometry/flow inline; set_config via review dialog. */
};

OllamaExecutionPolicy ollama_execution_policy_for_assist_mode();
OllamaExecutionPolicy ollama_execution_policy_for_assist_loop();

constexpr const char* kAssistantModeQuestion = "question";
constexpr const char* kAssistantModeAssist   = "assist";

OllamaExecutionPolicy ollama_execution_policy_from_mode_string(const std::string& mode);
std::string           ollama_mode_string_from_policy(OllamaExecutionPolicy policy);

bool ollama_action_requires_confirmation(const std::string& type, OllamaExecutionPolicy policy);
bool ollama_action_is_readonly(const std::string& type);

}} // namespace

#endif
