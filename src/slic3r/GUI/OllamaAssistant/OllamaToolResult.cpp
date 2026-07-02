#include "OllamaToolResult.hpp"

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
        return korean ? "메쉬를 수리했습니다." : "Repaired the mesh.";
    if (tool == "mirror_mesh" || msg.find("Mirrored mesh") != std::string::npos)
        return korean ? "모델을 대칭으로 만들었습니다." : "Mirrored the model.";
    if (tool == "mesh_boolean" || msg.find("Applied mesh operation") != std::string::npos) {
        if (msg.find("subtract") != std::string::npos || msg.find("drill") != std::string::npos)
            return korean ? "모델에 구멍을 뚫었습니다." : "Drilled a hole in the model.";
        if (msg.find("add_handle") != std::string::npos || msg.find("handle") != std::string::npos)
            return korean ? "손잡이를 추가했습니다." : "Added a handle to the model.";
        if (msg.find("add_rib") != std::string::npos)
            return korean ? "리브(보강)를 추가했습니다." : "Added reinforcement ribs.";
        return korean ? "모델 형상을 수정했습니다." : "Modified model geometry.";
    }
    if (tool == "scale" || msg.find("Scaled") != std::string::npos)
        return korean ? "모델 크기를 조정했습니다." : "Resized the model.";
    if (tool == "rotate" || msg.find("Rotat") != std::string::npos)
        return korean ? "모델을 회전했습니다." : "Rotated the model.";
    if (tool == "translate" || msg.find("Translat") != std::string::npos)
        return korean ? "모델 위치를 옮겼습니다." : "Moved the model.";
    if (tool == "arrange" || msg.find("rrang") != std::string::npos)
        return korean ? "판 위에 모델을 배치했습니다." : "Arranged models on the plate.";
    if (tool == "arrange_objects" || msg.find("by-object") != std::string::npos)
        return korean ? "객체 단위로 재배치했습니다." : "Arranged models by object.";
    if (tool == "split_object" || tool == "split_mesh" || msg.find("Split model") != std::string::npos)
        return korean ? "모델을 개별 객체로 분할했습니다." : "Split the model into separate objects.";
    if (tool == "add_plate" || msg.find("Added plate") != std::string::npos)
        return korean ? "새 빌드 플레이트를 추가했습니다." : "Added a new build plate.";
    if (tool == "set_config" || msg.find("print setting") != std::string::npos)
        return korean ? "인쇄 설정을 조정했습니다." : "Updated print settings.";
    if (tool == "get_state" || tool == "list_objects")
        return korean ? "모델 상태를 확인했습니다." : "Checked model state.";
    if (tool == "slice" || msg.find("licing") != std::string::npos)
        return korean ? "슬라이싱을 시작했습니다." : "Started slicing.";
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
            const std::string line = strip_leading_list_numbering(
                friendly_tool_line(tool, msg, korean));
            add_once(line);
        }
    }

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
    const std::string executed = ollama_format_agent_step_summary(step_tool_results, korean);
    if (!executed.empty())
        return executed;
    if (!step_tool_results.empty())
        return {};
    return llm_message;
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

namespace {

std::string describe_action_type(const std::string& type, const nlohmann::json& action, bool korean)
{
    if (type == "get_state" || type == "list_objects")
        return korean ? "현재 상태 확인" : "check current state";
    if (type == "set_config")
        return korean ? "인쇄 설정 변경" : "update print settings";
    if (type == "repair_mesh")
        return korean ? "메쉬 수리" : "repair mesh";
    if (type == "mesh_boolean") {
        const std::string op = action.value("operation", "");
        if (op.find("subtract") != std::string::npos)
            return korean ? "구멍 뚫기" : "drill hole";
        if (op.find("handle") != std::string::npos)
            return korean ? "손잡이 추가" : "add handle";
        return korean ? "형상 수정" : "edit geometry";
    }
    if (type == "mirror_mesh")
        return korean ? "대칭 만들기" : "mirror model";
    if (type == "scale")
        return korean ? "크기 조정" : "resize";
    if (type == "rotate")
        return korean ? "회전" : "rotate";
    if (type == "translate")
        return korean ? "이동" : "move";
    if (type == "arrange")
        return korean ? "자동 배치" : "arrange";
    if (type == "arrange_objects")
        return korean ? "객체 단위 재배치" : "arrange by object";
    if (type == "split_object" || type == "split_mesh")
        return korean ? "모델 분할" : "split objects";
    if (type == "add_plate")
        return korean ? "플레이트 추가" : "add plate";
    if (type == "slice")
        return korean ? "슬라이스" : "slice";
    return type;
}

} // namespace

std::string ollama_thinking_goal_intro(const std::string& user_goal, bool korean)
{
    (void) user_goal;
    return korean ? "요청을 확인했습니다. 필요한 변경을 찾아 적용하겠습니다."
                  : "Understood — I'll find and apply the needed changes.";
}

std::string ollama_thinking_planned_actions(const nlohmann::json& root, bool korean)
{
    if (!root.contains("actions") || !root["actions"].is_array() || root["actions"].empty())
        return {};

    std::vector<std::string> parts;
    for (const auto& a : root["actions"]) {
        if (!a.is_object())
            continue;
        const std::string type = a.value("type", "");
        if (type.empty() || type == "get_state" || type == "list_objects" || type == "select_object")
            continue;
        const std::string desc = describe_action_type(type, a, korean);
        if (desc.empty())
            continue;
        bool dup = false;
        for (const auto& p : parts)
            if (p == desc) {
                dup = true;
                break;
            }
        if (!dup)
            parts.push_back(desc);
    }
    if (parts.empty())
        return {};

    std::string joined;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            joined += korean ? " → " : " → ";
        joined += parts[i];
    }
    return korean ? (boost::format("적용합니다: %1%") % joined).str()
                  : (boost::format("Applying: %1%") % joined).str();
}

std::string ollama_thinking_step_wait(int step, bool korean)
{
    (void) step;
    return korean ? "슬라이서 상태를 확인하고 변경안을 준비하고 있습니다…"
                  : "Checking slicer state and preparing changes…";
}

std::string ollama_thinking_after_tools(const nlohmann::json& tool_results, bool korean)
{
    if (!tool_results.is_array() || tool_results.empty())
        return {};

    std::vector<std::string> lines;
    for (const auto& r : tool_results) {
        if (!r.is_object())
            continue;
        if (!r.value("ok", false)) {
            return korean ? "적용 중 문제가 있어 다른 방법을 시도합니다."
                          : "Something failed — trying another approach.";
        }
        if (!r.value("changed", false))
            continue;
        const std::string line =
            friendly_tool_line(r.value("tool", ""), r.value("message", ""), korean);
        if (!line.empty()) {
            bool dup = false;
            for (const auto& existing : lines)
                if (existing == line) {
                    dup = true;
                    break;
                }
            if (!dup)
                lines.push_back(line);
        }
    }

    if (lines.empty())
        return korean ? "상태를 확인했습니다." : "Checked the current state.";

    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i)
            out += korean ? " " : " ";
        out += lines[i];
    }
    return out;
}

}} // namespace
