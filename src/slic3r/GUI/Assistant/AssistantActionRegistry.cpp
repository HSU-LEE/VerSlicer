#include "AssistantActionRegistry.hpp"
#include "AssistantActionValidator.hpp"

#include <boost/algorithm/string.hpp>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

const AssistantActionTypeSpec kActionTypes[] = {
    {"get_state", true, true, true, "Read current slicer state"},
    {"find_setting", true, true, true, "Find a slicer setting by name. Pass query."},
    {"wiki_search", true, true, true, "Search Bambu Lab wiki for a query"},
    {"list_objects", true, true, true, "List models on the plate"},
    {"printer_control", true, false, false, "Set printer nozzle/bed/chamber temperature or print speed (user must allow)"},
    {"printer_diagnostics", true, true, true, "Read printer error codes and HMS alerts"},
    {"select_object", true, true, true, "Select object by id or name"},
    {"set_config", true, true, true, "Change print/filament/printer preset options"},
    {"camera_watch", true, false, false, "Report or turn on the printer's camera failure checks (user must allow)"},
    {"what_was_learned", true, true, true, "Read what Smart Print learned about the current printer"},
    {"show_weak_spots", true, true, true, "Show where the models are likely to fail, on the 3D view"},
    {"translate", true, true, true, "Move selection on the bed"},
    {"rotate", true, true, true, "Rotate selection"},
    {"scale", true, false, false, "Scale selection"},
    {"clone_selection", true, true, true, "Duplicate selection"},
    {"arrange", true, true, true, "Auto-arrange on build plate (by layer)"},
    {"arrange_objects", true, true, true, "Arrange by object (print-by-object layout)"},
    {"split_object", true, true, true, "Split selected model into separate objects"},
    {"split_mesh", true, true, true, "Split selected model into separate objects (alias)"},
    {"delete_selection", true, false, false, "Delete selected models"},
    {"ui_select_tab", true, false, false, "Switch main tab"},
    {"open_calibration", true, false, false, "Open calibration tab"},
    {"slice", true, false, false, "Slice current plate"},
    {"add_model", true, false, false, "Import local model file"},
    {"makerworld_search", true, false, false, "Search MakerWorld catalog"},
    {"makerworld_find_and_print", true, false, false, "Search MakerWorld top 3, import and slice (no send)"},
    {"import_makerworld", true, false, false, "Import from MakerWorld"},
    {"open_smart_print", true, false, true, "Open Smart Print tab/panel"},
    {"open_setup", true, false, true, "Open Smart Print setup wizard"},
    {"export_gcode", true, false, true, "Export G-code file"},
    {"add_plate", true, true, true, "Add a new build plate"},
    {"delete_plate", true, false, false, "Delete current plate"},
    {"select_plate", true, false, false, "Select plate by index"},
    {"save_project", true, false, false, "Save project"},
    {"select_preset", true, false, false, "Select print/filament/printer preset"},
    {"run_smart_print", true, false, true, "Open Smart Print (the user starts printing there)"},
    {"rollback_apply", true, false, true, "Undo last AI settings apply"},
    {"repair_mesh", true, true, true, "Repair STL mesh (non-manifold, holes, normals)"},
    {"mirror_mesh", true, true, true, "Mirror mesh on axis"},
};

const std::unordered_set<std::string> kBlockedTypes = {
    "quit",
    "exit",
    "menu_item",
};

} // namespace

const std::vector<AssistantActionTypeSpec>& AssistantActionRegistry::all()
{
    static const std::vector<AssistantActionTypeSpec> specs(std::begin(kActionTypes), std::end(kActionTypes));
    return specs;
}

bool AssistantActionRegistry::is_blocked_type(const std::string& type)
{
    return kBlockedTypes.find(type) != kBlockedTypes.end();
}

std::string AssistantActionValidator::canonical_action_type(std::string type)
{
    boost::algorithm::trim(type);
    boost::algorithm::to_lower(type);
    if (type == "split_mesh" || type == "split_objects" || type == "split")
        return "split_object";
    if (type == "add_palette" || type == "new_palette" || type == "new_plate" || type == "add_build_plate")
        return "add_plate";
    if (type == "arrange_by_object" || type == "arrange_by_objects" || type == "arrange-by-object"
        || type == "arrange_byobject" || type == "print_by_object" || type == "by_object")
        return "arrange_objects";
    if (type == "resize")
        return "scale";
    if (type == "search_setting" || type == "lookup_setting" || type == "find_settings")
        return "find_setting";
    return type;
}

bool AssistantActionRegistry::is_allowed_type(const std::string& type)
{
    if (is_blocked_type(type))
        return false;
    const std::string canon = AssistantActionValidator::canonical_action_type(type);
    for (const auto& spec : all()) {
        if (canon == spec.type || type == spec.type)
            return true;
    }
    return false;
}

bool AssistantActionRegistry::is_allowed_in_advisor(const std::string& type)
{
    if (is_blocked_type(type))
        return false;
    for (const auto& spec : all()) {
        if (type == spec.type)
            return spec.allowed_in_advisor;
    }
    return false;
}

std::string AssistantActionRegistry::action_types_prompt_block(bool /*ko_ui*/, bool apply_mode)
{
    std::string out;
    for (const auto& spec : all()) {
        if (!apply_mode && spec.type != std::string("set_config"))
            continue;
        if (apply_mode && !spec.allowed_in_apply)
            continue;
        out += "- ";
        out += spec.type;
        out += ": ";
        out += spec.description;
        out += "\n";
    }
    return out;
}

}} // namespace
