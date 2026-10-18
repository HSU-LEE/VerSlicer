#include "OllamaToolGateway.hpp"

#include "OllamaActionWorkflow.hpp"
#include "OllamaExecutionPolicy.hpp"
#include "OllamaToolResult.hpp"

namespace Slic3r { namespace GUI {

namespace {

nlohmann::json obj_schema(nlohmann::json properties, nlohmann::json required = nlohmann::json::array())
{
    // nlohmann::json{} is null. ChatGPT rejects a null properties object.
    if (!properties.is_object())
        properties = nlohmann::json::object();
    if (!required.is_array())
        required = nlohmann::json::array();
    return nlohmann::json{
        {"type", "object"},
        {"properties", std::move(properties)},
        {"required", std::move(required)},
        {"additionalProperties", false},
    };
}

nlohmann::json number_prop(const char* description)
{
    return nlohmann::json{{"type", "number"}, {"description", description}};
}

nlohmann::json tool_def(const char* name, const char* description, nlohmann::json schema)
{
    return nlohmann::json{
        {"name", name},
        {"description", description},
        {"inputSchema", std::move(schema)},
    };
}

const std::vector<nlohmann::json>& tool_defs()
{
    static const std::vector<nlohmann::json> defs = {
        tool_def("get_state",
                 "Read the plate, selection, model size_mm, position_mm, rotation_z_deg, and slice state.",
                 obj_schema({})),
        tool_def("list_objects",
                 "List models on the plate with size_mm, position_mm, and rotation_z_deg.",
                 obj_schema({})),
        tool_def("find_setting",
                 "Find a slicer setting by the user's words. Then call set_config with the returned key, preset, and enum value.",
                 obj_schema({{"query", {{"type", "string"}, {"description", "Words the user used for the setting"}}}},
                            nlohmann::json::array({"query"}))),
        tool_def("set_config",
                 "Change print, filament, or printer settings. options keys must come from find_setting. Does not slice.",
                 obj_schema({{"preset", {{"type", "string"}, {"enum", {"print", "filament", "printer"}}}},
                              {"options", {{"type", "object"}, {"description", "Setting key to value"}}}},
                            nlohmann::json::array({"options"}))),
        tool_def("select_object",
                 "Select a model by object_id or object_name.",
                 obj_schema({{"object_id", {{"type", "integer"}}},
                              {"object_name", {{"type", "string"}}}})),
        tool_def("transform",
                 "Change the selected model. rotate: z is degrees added around the bed. scale: factor is a multiplier (0.5 is half, 2 is double). translate: x, y, z are millimeters.",
                 obj_schema({{"op", {{"type", "string"}, {"enum", {"rotate", "scale", "translate"}}}},
                              {"x", number_prop("Degrees for rotate, millimeters for translate")},
                              {"y", number_prop("Degrees for rotate, millimeters for translate")},
                              {"z", number_prop("Degrees added around the bed for rotate, millimeters for translate")},
                              {"factor", number_prop("Scale multiplier. Numbers above 20 are percent.")},
                              {"absolute", {{"type", "boolean"}, {"description", "rotate z is a bed angle instead of a delta"}}}},
                            nlohmann::json::array({"op"}))),
        tool_def("slice", "Slice the current plate.", obj_schema({})),
        tool_def("export_gcode", "Export G-code for the sliced plate. Asks the user to confirm.", obj_schema({})),
        tool_def("send_print", "Send the sliced job to the printer. Asks the user to confirm.", obj_schema({})),
        tool_def("delete_selection", "Delete the selected models. Asks the user to confirm.", obj_schema({})),
    };
    return defs;
}

bool has_number(const nlohmann::json& args, const char* key)
{
    return args.contains(key) && args[key].is_number();
}

nlohmann::json parse_arguments(const nlohmann::json& arguments)
{
    if (arguments.is_object())
        return arguments;
    if (arguments.is_string()) {
        try {
            nlohmann::json parsed = nlohmann::json::parse(arguments.get<std::string>());
            if (parsed.is_object())
                return parsed;
        } catch (...) {
        }
    }
    return nlohmann::json::object();
}

} // namespace

nlohmann::json OllamaToolGateway::ollama_tools()
{
    nlohmann::json tools = nlohmann::json::array();
    for (const auto& def : tool_defs()) {
        tools.push_back({
            {"type", "function"},
            {"function",
             {{"name", def["name"]},
              {"description", def["description"]},
              {"parameters", def["inputSchema"]}}},
        });
    }
    return tools;
}

nlohmann::json OllamaToolGateway::mcp_tools()
{
    nlohmann::json tools = nlohmann::json::array();
    for (const auto& def : tool_defs())
        tools.push_back(def);
    return tools;
}

OllamaNormalizedTool OllamaToolGateway::normalize(const std::string& name, const nlohmann::json& arguments)
{
    OllamaNormalizedTool out;
    const nlohmann::json args = parse_arguments(arguments);

    auto known = false;
    for (const auto& def : tool_defs()) {
        if (def.value("name", "") == name) {
            known = true;
            break;
        }
    }
    if (!known) {
        out.error = "Unknown tool: " + name;
        return out;
    }

    if (name == "find_setting") {
        if (!args.contains("query") || !args["query"].is_string() || args["query"].get<std::string>().empty()) {
            out.error = "find_setting requires query";
            return out;
        }
        out.action = {{"type", "find_setting"}, {"query", args["query"]}};
    } else if (name == "set_config") {
        if (!args.contains("options") || !args["options"].is_object() || args["options"].empty()) {
            out.error = "set_config requires options";
            return out;
        }
        out.action = {{"type", "set_config"},
                      {"preset", args.value("preset", "print")},
                      {"options", args["options"]}};
    } else if (name == "select_object") {
        if (!has_number(args, "object_id") && !(args.contains("object_name") && args["object_name"].is_string())) {
            out.error = "select_object requires object_id or object_name";
            return out;
        }
        out.action = {{"type", "select_object"}};
        if (has_number(args, "object_id"))
            out.action["object_id"] = args["object_id"].get<int>();
        if (args.contains("object_name") && args["object_name"].is_string())
            out.action["object_name"] = args["object_name"];
    } else if (name == "transform") {
        const std::string op = args.value("op", "");
        if (op != "rotate" && op != "scale" && op != "translate") {
            out.error = "transform op must be rotate, scale, or translate";
            return out;
        }
        if (op == "rotate" && !has_number(args, "x") && !has_number(args, "y") && !has_number(args, "z")) {
            out.error = "rotate requires z (degrees added around the bed), or x or y";
            return out;
        }
        if (op == "scale" && !has_number(args, "factor")) {
            out.error = "scale requires factor";
            return out;
        }
        if (op == "translate" && !has_number(args, "x") && !has_number(args, "y") && !has_number(args, "z")) {
            out.error = "translate requires x, y, or z in millimeters";
            return out;
        }
        out.action = {{"type", op}};
        for (const char* key : {"x", "y", "z", "factor"}) {
            if (has_number(args, key))
                out.action[key] = args[key];
        }
        if (args.contains("absolute") && args["absolute"].is_boolean())
            out.action["absolute"] = args["absolute"];
    } else if (name == "get_state" || name == "list_objects" || name == "slice" || name == "export_gcode"
               || name == "send_print" || name == "delete_selection") {
        out.action = {{"type", name}};
    } else {
        out.error = "Unknown tool: " + name;
        return out;
    }

    out.ok = true;
    return out;
}

nlohmann::json OllamaToolGateway::call(const std::string& name, const nlohmann::json& arguments)
{
    const OllamaNormalizedTool normalized = normalize(name, arguments);
    if (!normalized.ok) {
        return nlohmann::json{
            {"content", nlohmann::json::array({{{"type", "text"}, {"text", normalized.error}}})},
            {"isError", true},
        };
    }

    nlohmann::json root = {
        {"message", ""},
        {"done", false},
        {"actions", nlohmann::json::array({normalized.action})},
    };
    const OllamaWorkflowRun run =
        OllamaActionWorkflow::execute_with_policy(root, nullptr, ollama_execution_policy_for_assist_loop());
    nlohmann::json results = ollama_tool_results_from_workflow(root, run);
    const bool     failed  = run.cancelled || results.empty();
    return nlohmann::json{
        {"content", nlohmann::json::array({{{"type", "text"}, {"text", results.dump(2)}}})},
        {"isError", failed},
    };
}

}} // namespace
