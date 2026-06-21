#include "OllamaToolResult.hpp"

#include <boost/format.hpp>

namespace Slic3r { namespace GUI {

namespace {

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

    if (korean) {
        if (failed > 0)
            return (boost::format("에이전트 완료: 변경 %1%건, 실패 %2%건.") % changed % failed).str();
        if (changed == 0)
            return "에이전트 완료: 적용된 변경 없음.";
        return (boost::format("에이전트 완료: %1%건 적용됨.") % changed).str();
    }
    if (failed > 0)
        return (boost::format("Agent done: %1% change(s), %2% failure(s).") % changed % failed).str();
    if (changed == 0)
        return "Agent done: no changes applied.";
    return (boost::format("Agent done: %1% change(s) applied.") % changed).str();
}

}} // namespace
