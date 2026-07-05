#include "OllamaActionPipelineCore.hpp"

#include <unordered_set>

namespace Slic3r { namespace GUI {

std::string OllamaActionPipelineCore::action_fingerprint(const nlohmann::json& action)
{
    if (!action.is_object() || !action.contains("type"))
        return {};
    const std::string type = action.value("type", "");
    if (type == "set_config") {
        std::string fp = type + "|" + action.value("preset", "print");
        if (action.contains("filament_index"))
            fp += "|f" + std::to_string(action.value("filament_index", 0));
        if (action.contains("options") && action["options"].is_object())
            fp += "|" + action["options"].dump();
        return fp;
    }
    if (type == "rotate" || type == "translate" || type == "scale")
        return type + "|" + action.dump();
    if (type == "repair_mesh" || type == "mirror_mesh")
        return type;
    if (type == "mesh_boolean")
        return type + "|" + action.value("operation", "");
    if (type == "arrange" || type == "arrange_objects" || type == "clone_selection" || type == "delete_selection"
        || type == "split_object")
        return type;
    if (type == "makerworld_search")
        return type + "|" + action.value("query", "");
    if (type == "makerworld_find_and_print")
        return type + "|" + action.value("query", "");
    if (type == "import_makerworld")
        return type + "|" + action.value("url", "");
    return type + "|" + action.dump();
}

void OllamaActionPipelineCore::dedupe_actions_in_turn(nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    std::unordered_set<std::string> seen;
    nlohmann::json                  kept = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        const std::string fp = action_fingerprint(a);
        if (fp.empty() || seen.insert(fp).second)
            kept.push_back(a);
    }
    root["actions"] = std::move(kept);
}

}} // namespace
