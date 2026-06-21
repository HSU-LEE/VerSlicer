#include "OllamaExecutionPolicy.hpp"

#include "OllamaActionRegistry.hpp"

namespace Slic3r { namespace GUI {

OllamaExecutionPolicy ollama_execution_policy_for_assist_mode()
{
    return OllamaExecutionPolicy::ConfirmAlways;
}

OllamaExecutionPolicy ollama_execution_policy_for_agent_mode()
{
    return OllamaExecutionPolicy::AutoSafe;
}

OllamaExecutionPolicy ollama_execution_policy_from_mode_string(const std::string& mode)
{
    if (mode == kAssistantModeAgent)
        return OllamaExecutionPolicy::AutoSafe;
    if (mode == kAssistantModeAssist || mode == "apply")
        return OllamaExecutionPolicy::ConfirmAlways;
    return OllamaExecutionPolicy::ConfirmAlways;
}

std::string ollama_mode_string_from_policy(OllamaExecutionPolicy policy)
{
    switch (policy) {
    case OllamaExecutionPolicy::AgentAutonomous:
    case OllamaExecutionPolicy::AutoSafe:
        return kAssistantModeAgent;
    case OllamaExecutionPolicy::ConfirmAlways:
    default:
        return kAssistantModeAssist;
    }
}

bool ollama_action_is_readonly(const std::string& type)
{
    return type == "get_state" || type == "list_objects";
}

bool ollama_action_requires_confirmation(const std::string& type, OllamaExecutionPolicy policy)
{
    if (ollama_action_is_readonly(type))
        return false;
    if (type == "delete_selection" || type == "save_project" || type == "send_print")
        return true;
    if (type == "set_config")
        return policy != OllamaExecutionPolicy::AgentAutonomous;
    if (policy == OllamaExecutionPolicy::ConfirmAlways)
        return type == "export_gcode" || type == "import_makerworld";
    return false;
}

}} // namespace
