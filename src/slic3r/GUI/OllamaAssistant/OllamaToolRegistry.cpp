#include "OllamaToolRegistry.hpp"

#include "OllamaActionRegistry.hpp"
#include "OllamaActionValidator.hpp"

namespace Slic3r { namespace GUI {

namespace {

const OllamaToolSpec kTools[] = {
    {"get_state", OllamaToolCategory::Readonly, "Read current slicer state (selection, plates, slice, model size and rotation)"},
    {"find_setting", OllamaToolCategory::Readonly,
     "Find any slicer setting by the user's words. Pass query. Then set_config with the returned key, preset, and enum value."},
    {"wiki_search", OllamaToolCategory::Readonly, "Search the printer wiki. Pass query."},
    {"list_objects", OllamaToolCategory::Readonly, "List models on the plate"},
    {"printer_diagnostics", OllamaToolCategory::Readonly,
     "Read the printer's current error code, HMS alerts and print stage, with suspect areas and settings to check. Call first when the user says a print failed."},
    {"printer_control", OllamaToolCategory::Mutating,
     "Change the connected printer: target nozzle_temp, bed_temp or chamber_temp (value in °C, 0 = off), or print_speed "
     "(silent/standard/sport/ludicrous, while printing). Only when the user asks. The user must allow it once; limits are enforced."},
    {"select_object", OllamaToolCategory::Mutating, "Select object by id or name"},
    {"set_config", OllamaToolCategory::Dangerous, "Change print settings"},
    {"arrange", OllamaToolCategory::Mutating, "Auto-arrange on plate (by layer)"},
    {"arrange_objects", OllamaToolCategory::Mutating, "Arrange by object / print-by-object layout"},
    {"split_mesh", OllamaToolCategory::Mutating, "Split model into separate objects"},
    {"split_object", OllamaToolCategory::Mutating, "Split model into separate objects (alias)"},
    {"rotate", OllamaToolCategory::Mutating,
     "Rotate the selected model. z is degrees added around the bed (45° turn: {\"type\":\"rotate\",\"z\":45}). angle is accepted as z."},
    {"translate", OllamaToolCategory::Mutating, "Move selection"},
    {"slice", OllamaToolCategory::Mutating, "Slice current plate"},
    {"ui_select_tab", OllamaToolCategory::Mutating, "Switch main tab"},
    {"open_calibration", OllamaToolCategory::Mutating, "Open calibration tab"},
    {"export_gcode", OllamaToolCategory::Dangerous, "Export G-code"},
    {"add_plate", OllamaToolCategory::Mutating, "Add a new build plate (palette)"},
    {"delete_plate", OllamaToolCategory::Dangerous, "Delete current plate"},
    {"select_plate", OllamaToolCategory::Mutating, "Select plate by index"},
    {"save_project", OllamaToolCategory::Dangerous, "Save project"},
    {"select_preset", OllamaToolCategory::Dangerous, "Select print/filament/printer preset"},
    {"run_smart_print", OllamaToolCategory::Mutating, "Open Smart Print workflow"},
    {"makerworld_search", OllamaToolCategory::Mutating, "Search MakerWorld"},
    {"makerworld_find_and_print", OllamaToolCategory::Mutating,
     "Search MakerWorld (top 3), let the user pick, then import and slice. Does not send to the printer."},
    {"rollback_apply", OllamaToolCategory::Mutating, "Undo last AI apply"},
    {"scale", OllamaToolCategory::Mutating,
     "Resize the selection. factor is a multiplier (half size: 0.5, double: 2). A number above 20 is a percent (50 → 0.5). Not set_config."},
    {"clone_selection", OllamaToolCategory::Mutating, "Duplicate selection"},
    {"delete_selection", OllamaToolCategory::Dangerous, "Delete selection (only if user asked)"},
    {"repair_mesh", OllamaToolCategory::Mutating, "Repair mesh defects (check mesh_health first)"},
    {"mirror_mesh", OllamaToolCategory::Mutating, "Mirror mesh"},
};

} // namespace

const std::vector<OllamaToolSpec>& OllamaToolRegistry::all()
{
    static const std::vector<OllamaToolSpec> specs(std::begin(kTools), std::end(kTools));
    return specs;
}

OllamaToolCategory OllamaToolRegistry::category_for(const std::string& tool_id)
{
    const std::string canon = OllamaActionValidator::canonical_action_type(tool_id);
    for (const auto& t : all()) {
        if (tool_id == t.id || canon == t.id)
            return t.category;
    }
    if (OllamaActionRegistry::is_allowed_type(canon))
        return OllamaToolCategory::Mutating;
    return OllamaToolCategory::Dangerous;
}

bool OllamaToolRegistry::is_observation(const std::string& tool_id)
{
    return tool_id == "select_object" || category_for(tool_id) == OllamaToolCategory::Readonly;
}

bool OllamaToolRegistry::is_agent_tool(const std::string& tool_id)
{
    if (tool_id == "get_state" || tool_id == "list_objects")
        return true;
    const std::string canon = OllamaActionValidator::canonical_action_type(tool_id);
    return OllamaActionRegistry::is_allowed_type(canon);
}

std::string OllamaToolRegistry::agent_tools_schema_block(bool /*korean*/)
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
