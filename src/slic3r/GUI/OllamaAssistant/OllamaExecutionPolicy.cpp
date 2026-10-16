#include "OllamaExecutionPolicy.hpp"

#include "OllamaActionRegistry.hpp"

namespace Slic3r { namespace GUI {

OllamaExecutionPolicy ollama_execution_policy_for_assist_mode()
{
    return OllamaExecutionPolicy::ConfirmAlways;
}

OllamaExecutionPolicy ollama_execution_policy_for_assist_loop()
{
    return OllamaExecutionPolicy::AutoSafe;
}

OllamaExecutionPolicy ollama_execution_policy_from_mode_string(const std::string& mode)
{
    (void) mode;
    return OllamaExecutionPolicy::ConfirmAlways;
}

std::string ollama_mode_string_from_policy(OllamaExecutionPolicy policy)
{
    (void) policy;
    return kAssistantModeAssist;
}

bool ollama_action_is_readonly(const std::string& type)
{
    return type == "get_state" || type == "list_objects" || type == "wiki_search" || type == "find_setting";
}

bool ollama_action_requires_confirmation(const std::string& type, OllamaExecutionPolicy policy)
{
    if (ollama_action_is_readonly(type))
        return false;
    if (type == "delete_selection" || type == "delete_plate" || type == "save_project"
        || type == "send_print" || type == "export_gcode" || type == "set_config")
        return true;
    if (policy == OllamaExecutionPolicy::ConfirmAlways)
        return type == "import_makerworld";
    return false;
}

}} // namespace
