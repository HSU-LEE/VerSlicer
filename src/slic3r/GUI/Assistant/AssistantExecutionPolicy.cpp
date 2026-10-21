#include "AssistantExecutionPolicy.hpp"

#include "AssistantActionRegistry.hpp"
#include "AssistantToolRegistry.hpp"

namespace Slic3r { namespace GUI {

AssistantExecutionPolicy assistant_execution_policy_for_assist_mode()
{
    return AssistantExecutionPolicy::ConfirmAlways;
}

AssistantExecutionPolicy assistant_execution_policy_for_assist_loop()
{
    return AssistantExecutionPolicy::AutoSafe;
}

AssistantExecutionPolicy assistant_execution_policy_from_mode_string(const std::string& mode)
{
    (void) mode;
    return AssistantExecutionPolicy::ConfirmAlways;
}

std::string assistant_mode_string_from_policy(AssistantExecutionPolicy policy)
{
    (void) policy;
    return kAssistantModeAssist;
}

bool assistant_action_is_readonly(const std::string& type)
{
    return AssistantToolRegistry::category_for(type) == AssistantToolCategory::Readonly;
}

bool assistant_action_requires_confirmation(const std::string& type, AssistantExecutionPolicy policy)
{
    if (assistant_action_is_readonly(type))
        return false;
    if (AssistantToolRegistry::category_for(type) == AssistantToolCategory::Dangerous)
        return true;
    if (policy == AssistantExecutionPolicy::ConfirmAlways)
        return type == "import_makerworld";
    return false;
}

}} // namespace
