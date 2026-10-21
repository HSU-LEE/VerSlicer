#include "AssistantToolRegistry.hpp"

#include "AssistantActionRegistry.hpp"
#include "AssistantActionValidator.hpp"

namespace Slic3r { namespace GUI {

namespace {

const AssistantToolSpec kTools[] = {
    {"get_state", AssistantToolCategory::Readonly, "Read current slicer state (selection, plates, slice, model size and rotation)"},
    {"find_setting", AssistantToolCategory::Readonly,
     "Find any slicer setting by the user's words. Pass query. Then set_config with the returned key, preset, and enum value."},
    {"wiki_search", AssistantToolCategory::Readonly, "Search the printer wiki. Pass query."},
    {"list_objects", AssistantToolCategory::Readonly, "List models on the plate"},
    {"printer_diagnostics", AssistantToolCategory::Readonly,
     "Read the printer's current error code, HMS alerts and print stage, with suspect areas and settings to check. Call first when the user says a print failed."},
    {"printer_control", AssistantToolCategory::Mutating,
     "Change the connected printer: target nozzle_temp, bed_temp or chamber_temp (value in °C, 0 = off), or print_speed "
     "(silent/standard/sport/ludicrous, while printing). Only when the user asks. The user must allow it once; limits are enforced."},
    {"select_object", AssistantToolCategory::Mutating, "Select object by id or name"},
    {"set_config", AssistantToolCategory::Dangerous, "Change print settings"},
    {"arrange", AssistantToolCategory::Mutating, "Auto-arrange on plate (by layer)"},
    {"arrange_objects", AssistantToolCategory::Mutating, "Arrange by object / print-by-object layout"},
    {"split_mesh", AssistantToolCategory::Mutating, "Split model into separate objects"},
    {"split_object", AssistantToolCategory::Mutating, "Split model into separate objects (alias)"},
    {"rotate", AssistantToolCategory::Mutating,
     "Rotate the selected model. z is degrees added around the bed (45° turn: {\"type\":\"rotate\",\"z\":45}). angle is accepted as z."},
    {"translate", AssistantToolCategory::Mutating, "Move selection"},
    {"slice", AssistantToolCategory::Mutating, "Slice current plate"},
    {"ui_select_tab", AssistantToolCategory::Mutating, "Switch main tab"},
    {"open_calibration", AssistantToolCategory::Mutating, "Open calibration tab"},
    {"export_gcode", AssistantToolCategory::Dangerous, "Export G-code"},
    {"add_plate", AssistantToolCategory::Mutating, "Add a new build plate (palette)"},
    {"delete_plate", AssistantToolCategory::Dangerous, "Delete current plate"},
    {"select_plate", AssistantToolCategory::Mutating, "Select plate by index"},
    {"save_project", AssistantToolCategory::Dangerous, "Save project"},
    {"select_preset", AssistantToolCategory::Dangerous, "Select print/filament/printer preset"},
    {"run_smart_print", AssistantToolCategory::Mutating, "Open Smart Print workflow"},
    {"makerworld_search", AssistantToolCategory::Mutating, "Search MakerWorld"},
    {"makerworld_find_and_print", AssistantToolCategory::Mutating,
     "Search MakerWorld (top 3), let the user pick, then import and slice. Does not send to the printer."},
    {"rollback_apply", AssistantToolCategory::Mutating, "Undo last AI apply"},
    {"scale", AssistantToolCategory::Mutating,
     "Resize the selection. factor is a multiplier (half size: 0.5, double: 2). A number above 20 is a percent (50 → 0.5). Not set_config."},
    {"clone_selection", AssistantToolCategory::Mutating, "Duplicate selection"},
    {"delete_selection", AssistantToolCategory::Dangerous, "Delete selection (only if user asked)"},
    {"repair_mesh", AssistantToolCategory::Mutating, "Repair mesh defects (check mesh_health first)"},
    {"mirror_mesh", AssistantToolCategory::Mutating, "Mirror mesh"},
};

} // namespace

const std::vector<AssistantToolSpec>& AssistantToolRegistry::all()
{
    static const std::vector<AssistantToolSpec> specs(std::begin(kTools), std::end(kTools));
    return specs;
}

AssistantToolCategory AssistantToolRegistry::category_for(const std::string& tool_id)
{
    const std::string canon = AssistantActionValidator::canonical_action_type(tool_id);
    for (const auto& t : all()) {
        if (tool_id == t.id || canon == t.id)
            return t.category;
    }
    if (AssistantActionRegistry::is_allowed_type(canon))
        return AssistantToolCategory::Mutating;
    return AssistantToolCategory::Dangerous;
}

bool AssistantToolRegistry::is_observation(const std::string& tool_id)
{
    return tool_id == "select_object" || category_for(tool_id) == AssistantToolCategory::Readonly;
}

bool AssistantToolRegistry::is_agent_tool(const std::string& tool_id)
{
    if (tool_id == "get_state" || tool_id == "list_objects")
        return true;
    const std::string canon = AssistantActionValidator::canonical_action_type(tool_id);
    return AssistantActionRegistry::is_allowed_type(canon);
}

std::string AssistantToolRegistry::agent_tools_schema_block(bool /*korean*/)
{
    std::string out = "Available tools (actions[].type):\n";
    for (const auto& t : all()) {
        out += "- ";
        out += t.id;
        out += ": ";
        out += t.description;
        out += "\n";
    }
    out += "\nAgent reply JSON: {\"message\":\"...\", \"done\":false, \"actions\":[{\"type\":\"get_state\"}, ...]}\n"
          "Model-shape requests (resize, rotate) use scale/rotate only. You do not create geometry or add holes or handles — no set_config or slice unless user asked to slice.\n"
          "Set done:true with summary when the goal is complete.\n";
    return out;
}

}} // namespace
