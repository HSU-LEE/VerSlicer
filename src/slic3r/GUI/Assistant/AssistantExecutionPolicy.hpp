#ifndef slic3r_AssistantExecutionPolicy_hpp_
#define slic3r_AssistantExecutionPolicy_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** How AI actions pass through confirmation dialogs and inline execution. */
enum class AssistantExecutionPolicy
{
    ConfirmAlways, /** Always show review dialog for set_config. */
    AutoSafe,      /** Geometry/flow inline; set_config via review dialog. */
};

AssistantExecutionPolicy assistant_execution_policy_for_assist_mode();
AssistantExecutionPolicy assistant_execution_policy_for_assist_loop();

constexpr const char* kAssistantModeQuestion = "question";
constexpr const char* kAssistantModeAssist   = "assist";

AssistantExecutionPolicy assistant_execution_policy_from_mode_string(const std::string& mode);
std::string           assistant_mode_string_from_policy(AssistantExecutionPolicy policy);

bool assistant_action_requires_confirmation(const std::string& type, AssistantExecutionPolicy policy);
bool assistant_action_is_readonly(const std::string& type);

}} // namespace

#endif
