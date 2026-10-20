#include "OllamaToolResult.hpp"
#include "AiCatalog.hpp"
#include "OllamaToolRegistry.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

namespace Slic3r { namespace GUI {

namespace {

std::string strip_leading_list_numbering(std::string s)
{
    boost::algorithm::trim(s);
    size_t i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
        ++i;
    if (i > 0 && i < s.size() && (s[i] == '.' || s[i] == ')') && i + 1 < s.size()) {
        s = s.substr(i + 1);
        boost::algorithm::trim(s);
    }
    return s;
}

std::string action_type_at(const nlohmann::json& root, size_t index)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return {};
    if (index >= root["actions"].size())
        return {};
    const auto& a = root["actions"][index];
    if (!a.is_object())
        return {};
    return a.value("type", "");
}

} // namespace

nlohmann::json ollama_tool_result_json(const std::string& tool, const OllamaActionResult& result,
                                       const nlohmann::json& payload)
{
    nlohmann::json j = nlohmann::json::object({
        {"tool", tool},
        {"ok", result.success},
        {"changed", result.effective_change},
        {"message", result.message},
    });
    if (!payload.empty())
        j["data"] = payload;
    if (!result.success && !result.message.empty())
        j["blocker"] = result.message;
    return j;
}

nlohmann::json ollama_tool_results_from_executor(const nlohmann::json& root,
                                                 const std::vector<OllamaActionResult>& results)
{
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < results.size(); ++i) {
        const std::string tool = action_type_at(root, i);
        nlohmann::json    payload;
        if (tool == "set_config" && root.contains("actions") && root["actions"].is_array() && i < root["actions"].size()) {
            const auto& action = root["actions"][i];
            if (action.is_object() && action.contains("options") && action["options"].is_object())
                payload["options"] = action["options"];
        }
        arr.push_back(ollama_tool_result_json(tool.empty() ? "action" : tool, results[i], payload));
    }
    return arr;
}

nlohmann::json ollama_tool_results_from_workflow(const nlohmann::json& root, const OllamaWorkflowRun& run)
{
    nlohmann::json arr = ollama_tool_results_from_executor(root, run.results);
    if (run.cancelled) {
        arr.push_back(nlohmann::json::object({
            {"tool", "workflow"},
            {"ok", false},
            {"changed", false},
            {"message", "User cancelled or preview only"},
            {"blocker", "cancelled"},
        }));
    } else if (run.preview_only) {
        arr.push_back(nlohmann::json::object({
            {"tool", "workflow"},
            {"ok", true},
            {"changed", false},
            {"message", "Preview only — not applied"},
        }));
    }
    return arr;
}

namespace {

std::string friendly_tool_line(const std::string& tool, const std::string& msg, bool korean)
{
    if (tool == "repair_mesh" || msg.find("Repaired mesh") != std::string::npos)
        return ai_catalog_text("Repaired the mesh.");
    if (tool == "mirror_mesh" || msg.find("Mirrored mesh") != std::string::npos)
        return ai_catalog_text("Mirrored the model.");
    if (tool == "scale" || msg.find("Scaled") != std::string::npos)
        return ai_catalog_text("Resized the model.");
    if (tool == "rotate" || msg.find("Rotat") != std::string::npos) {
        const auto deg = msg.find("by ");
        const auto end = msg.find("°");
        if (deg != std::string::npos && end != std::string::npos && end > deg + 3) {
            const std::string n = msg.substr(deg + 3, end - (deg + 3));
            return (boost::format(ai_catalog_text("Rotated the model %s degrees.")) % n).str();
        }
        return ai_catalog_text("Rotated the model.");
    }
    if (tool == "translate" || msg.find("Translat") != std::string::npos)
        return ai_catalog_text("Moved the model.");
    if (tool == "arrange" || msg.find("rrang") != std::string::npos)
        return ai_catalog_text("Arranged models on the plate.");
    if (tool == "arrange_objects" || msg.find("by-object") != std::string::npos)
        return ai_catalog_text("Arranged models by object.");
    if (tool == "split_object" || tool == "split_mesh" || msg.find("Split model") != std::string::npos)
        return ai_catalog_text("Split the model into separate objects.");
    if (tool == "add_plate" || msg.find("Added plate") != std::string::npos)
        return ai_catalog_text("Added a new build plate.");
    if (tool == "set_config" || msg.find("print setting") != std::string::npos) {
        if (msg.rfind("Applied ", 0) == 0) {
            const std::string values = msg.substr(8);
            return (boost::format(ai_catalog_text("Applied %s")) % values).str();
        }
        if (msg.find("did not stick") != std::string::npos)
            return ai_catalog_text("The setting did not stay applied.");
        return ai_catalog_text("Updated print settings.");
    }
    if (tool == "get_state" || tool == "list_objects")
        return ai_catalog_text("Checked model state.");
    if (tool == "slice" || msg.find("licing") != std::string::npos)
        return ai_catalog_text("Started slicing.");
    if (!msg.empty())
        return msg;
    return {};
}

} // namespace

std::string ollama_format_agent_step_summary(const std::vector<nlohmann::json>& step_results, bool korean)
{
    std::vector<std::string> lines;
    auto add_once = [&](const std::string& line) {
        if (line.empty())
            return;
        for (const auto& existing : lines)
            if (existing == line)
                return;
        lines.push_back(line);
    };

    bool settings_changed = false;
    for (const auto& step : step_results) {
        if (!step.is_array() || step.empty())
            continue;
        for (const auto& r : step) {
            if (!r.is_object() || !r.value("ok", false) || !r.value("changed", false))
                continue;
            const std::string tool = r.value("tool", "");
            const std::string msg  = r.value("message", "");
            if (tool == "get_state" || tool == "list_objects" || tool == "select_object")
                continue;
            if (tool == "set_config")
                settings_changed = true;
            const std::string line = strip_leading_list_numbering(
                friendly_tool_line(tool, msg, korean));
            add_once(line);
        }
    }
    if (settings_changed)
        add_once(ai_catalog_text("If those settings are wrong, say “undo the last settings change”."));

    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i)
            out += "\n";
        out += (boost::format("%1%. %2%") % (i + 1) % lines[i]).str();
    }
    return out;
}

std::string ollama_user_facing_summary(const std::vector<nlohmann::json>& step_tool_results,
                                       const std::string& llm_message, bool korean)
{
    return ollama_verified_user_message(step_tool_results, llm_message, korean);
}

std::string ollama_verified_user_message(const std::vector<nlohmann::json>& step_tool_results,
                                         const std::string& assistant_msg, bool korean)
{
    std::vector<std::string> changed_lines;
    std::vector<std::string> failed_lines;
    bool saw_tool = false;
    bool saw_mutating = false;
    auto add_once = [](std::vector<std::string>& lines, const std::string& line) {
        if (line.empty())
            return;
        for (const auto& existing : lines)
            if (existing == line)
                return;
        lines.push_back(line);
    };
    for (const auto& step : step_tool_results) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (!r.is_object())
                continue;
            saw_tool = true;
            const std::string tool = r.value("tool", "");
            const bool readonly = OllamaToolRegistry::is_observation(tool) || tool == "verify_config";
            if (!readonly)
                saw_mutating = true;
            if (!r.value("ok", true)) {
                add_once(failed_lines, ai_catalog_text("The requested change did not apply."));
                continue;
            }
            if (!r.value("changed", false) || readonly)
                continue;
            add_once(changed_lines, friendly_tool_line(tool, r.value("message", ""), korean));
        }
    }
    if (!changed_lines.empty()) {
        std::string out;
        for (const auto& line : changed_lines) {
            if (!out.empty())
                out += "\n";
            out += line;
        }
        return out;
    }
    if (!failed_lines.empty())
        return failed_lines.front();
    if (saw_mutating)
        return ai_catalog_text("Nothing was applied.");
    if (saw_tool && !assistant_msg.empty())
        return assistant_msg;
    if (!saw_tool)
        return assistant_msg;
    return ai_catalog_text("Nothing was applied.");
}

std::string ollama_format_agent_completion_report(const std::vector<nlohmann::json>& step_results, bool korean)
{
    int changed = 0;
    int failed  = 0;
    std::vector<std::string> lines;
    for (const auto& step : step_results) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (!r.is_object())
                continue;
            if (r.value("ok", false) && r.value("changed", false))
                ++changed;
            if (!r.value("ok", true))
                ++failed;
            const std::string msg = r.value("message", "");
            if (!msg.empty() && r.value("changed", false))
                lines.push_back(msg);
        }
    }

    if (failed > 0)
        return (boost::format(ai_catalog_text("Agent done: %s change(s), %s failure(s).")) % changed % failed).str();
    if (changed == 0)
        return ai_catalog_text("Agent done: no changes applied.");
    return (boost::format(ai_catalog_text("Agent done: %s change(s) applied.")) % changed).str();
}

namespace {

std::string describe_action_type(const std::string& type, const nlohmann::json& action, bool korean)
{
    if (type == "get_state" || type == "list_objects")
        return ai_catalog_text("check current state");
    if (type == "set_config")
        return ai_catalog_text("update print settings");
    if (type == "repair_mesh")
        return ai_catalog_text("repair mesh");
    if (type == "mirror_mesh")
        return ai_catalog_text("mirror model");
    if (type == "scale")
        return ai_catalog_text("resize");
    if (type == "rotate")
        return ai_catalog_text("rotate");
    if (type == "translate")
        return ai_catalog_text("move");
    if (type == "arrange")
        return ai_catalog_text("arrange");
    if (type == "arrange_objects")
        return ai_catalog_text("arrange by object");
    if (type == "split_object" || type == "split_mesh")
        return ai_catalog_text("split objects");
    if (type == "add_plate")
        return ai_catalog_text("add plate");
    if (type == "slice")
        return ai_catalog_text("slice");
    return type;
}

std::string setting_activity_phrase(const nlohmann::json& action, bool korean)
{
    if (!action.contains("options") || !action["options"].is_object() || action["options"].empty())
        return ai_catalog_text("Updating print settings.");

    auto name_of = [korean](const std::string& key) -> std::string {
        if (key == "layer_height") return ai_catalog_text("layer height");
        if (key == "sparse_infill_density") return ai_catalog_text("infill");
        if (key == "enable_support") return ai_catalog_text("supports");
        if (key == "brim_width" || key == "brim_type") return ai_catalog_text("brim");
        if (key == "wall_loops") return ai_catalog_text("wall loops");
        if (key == "nozzle_temperature") return ai_catalog_text("nozzle temperature");
        if (key == "bed_temperature") return ai_catalog_text("bed temperature");
        if (key == "outer_wall_speed") return ai_catalog_text("outer wall speed");
        if (key == "retraction_length") return ai_catalog_text("retraction");
        return {};
    };

    std::string joined;
    int         count = 0;
    for (auto it = action["options"].begin(); it != action["options"].end() && count < 3; ++it, ++count) {
        std::string name = name_of(it.key());
        if (name.empty())
            name = it.key();
        if (it.value().is_number())
            name += ai_catalog_text(" to ") + it.value().dump();
        else if (it.value().is_string())
            name += ai_catalog_text(" to ") + it.value().get<std::string>();
        else if (it.value().is_boolean())
            name += it.value().get<bool>() ? (ai_catalog_text(" on")) : (ai_catalog_text(" off"));
        if (!joined.empty())
            joined += ", ";
        joined += name;
    }
    return (boost::format(ai_catalog_text("Changing %s.")) % joined).str();
}

std::string activity_line_for_action(const nlohmann::json& action, bool korean)
{
    const std::string type = action.value("type", "");
    if (type.empty())
        return {};
    if (type == "get_state")
        return ai_catalog_text("Looking at the slicer state.");
    if (type == "list_objects")
        return ai_catalog_text("Checking the models on the plate.");
    if (type == "select_object")
        return ai_catalog_text("Selecting the model to work on.");
    if (type == "rotate") {
        const double z = action.value("z", action.value("angle", 0.0));
        if (std::abs(z) > 0.01)
            return (boost::format(ai_catalog_text("Rotating the model %s degrees.")) % z).str();
        return ai_catalog_text("Rotating the model.");
    }
    if (type == "scale")
        return ai_catalog_text("Resizing the model.");
    if (type == "translate")
        return ai_catalog_text("Moving the model.");
    if (type == "find_setting") {
        const std::string query = action.value("query", action.value("name", ""));
        if (query.empty())
            return ai_catalog_text("Looking up the setting name.");
        return (boost::format(ai_catalog_text("Looking up the setting \"%s\".")) % query).str();
    }
    if (type == "printer_diagnostics")
        return ai_catalog_text("Checking the printer's error state.");
    if (type == "wiki_search") {
        const std::string query = action.value("query", "");
        if (query.empty())
            return ai_catalog_text("Searching the help pages.");
        return (boost::format(ai_catalog_text("Searching help for \"%s\".")) % query).str();
    }
    if (type == "set_config")
        return setting_activity_phrase(action, korean);
    const std::string desc = describe_action_type(type, action, korean);
    if (desc.empty())
        return {};
    return (boost::format(ai_catalog_text("Running: %s.")) % desc).str();
}

} // namespace

std::string ollama_thinking_goal_intro(const std::string& user_goal, bool korean)
{
    std::string goal = user_goal;
    boost::algorithm::trim(goal);
    if (goal.size() > 80)
        goal = goal.substr(0, 80) + "…";
    if (goal.empty())
        return ai_catalog_text("Read the request.");
    return (boost::format(ai_catalog_text("Request: %s")) % goal).str();
}

std::string ollama_thinking_planned_actions(const nlohmann::json& root, bool korean)
{
    if (!root.contains("actions") || !root["actions"].is_array() || root["actions"].empty())
        return {};

    std::string out;
    for (const auto& a : root["actions"]) {
        if (!a.is_object())
            continue;
        const std::string line = activity_line_for_action(a, korean);
        if (line.empty())
            continue;
        if (!out.empty())
            out += '\n';
        out += line;
    }
    return out;
}

std::string ollama_thinking_step_wait(int step, bool korean)
{
    (void) step;
    return ai_catalog_text("Checking slicer state and preparing changes…");
}

std::string ollama_thinking_after_tools(const nlohmann::json& tool_results, bool korean)
{
    if (!tool_results.is_array() || tool_results.empty())
        return {};

    std::string out;
    auto        add = [&](const std::string& line) {
        if (line.empty())
            return;
        if (!out.empty())
            out += '\n';
        out += line;
    };

    for (const auto& r : tool_results) {
        if (!r.is_object())
            continue;
        const std::string tool = r.value("tool", "");
        if (OllamaToolRegistry::is_observation(tool))
            continue;
        if (!r.value("ok", false)) {
            add(ai_catalog_text("That step did not apply. Trying another way."));
            continue;
        }
        if (!r.value("changed", false)) {
            if (tool == "set_config")
                add(ai_catalog_text("That setting was already at the requested value."));
            continue;
        }
        add(friendly_tool_line(tool, r.value("message", ""), korean));
    }
    return out;
}

}} // namespace
