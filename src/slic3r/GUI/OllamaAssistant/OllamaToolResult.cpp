#include "OllamaToolResult.hpp"
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
    if (tool == "rotate" || msg.find("Rotat") != std::string::npos) {
        const auto deg = msg.find("by ");
        const auto end = msg.find("°");
        if (deg != std::string::npos && end != std::string::npos && end > deg + 3) {
            const std::string n = msg.substr(deg + 3, end - (deg + 3));
            return korean ? ("모델을 " + n + "도 돌렸습니다.") : ("Rotated the model " + n + " degrees.");
        }
        return korean ? "모델을 회전했습니다." : "Rotated the model.";
    }
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
    if (tool == "set_config" || msg.find("print setting") != std::string::npos) {
        if (msg.rfind("Applied ", 0) == 0) {
            const std::string values = msg.substr(8);
            return korean ? ("설정을 반영했습니다: " + values) : ("Applied " + values);
        }
        if (msg.find("did not stick") != std::string::npos)
            return korean ? "설정이 반영되지 않았습니다." : "The setting did not stay applied.";
        return korean ? "인쇄 설정을 조정했습니다." : "Updated print settings.";
    }
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
        add_once(korean ? "설정이 마음에 들지 않으면 “방금 설정 되돌려 줘”라고 하세요."
                        : "If those settings are wrong, say “undo the last settings change”.");

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
                add_once(failed_lines, korean ? "요청한 변경이 적용되지 않았습니다."
                                              : "The requested change did not apply.");
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
        return korean ? "적용된 변경이 없습니다." : "Nothing was applied.";
    if (saw_tool && !assistant_msg.empty())
        return assistant_msg;
    if (!saw_tool)
        return assistant_msg;
    return korean ? "적용된 변경이 없습니다." : "Nothing was applied.";
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

std::string setting_activity_phrase(const nlohmann::json& action, bool korean)
{
    if (!action.contains("options") || !action["options"].is_object() || action["options"].empty())
        return korean ? "인쇄 설정을 바꿉니다." : "Updating print settings.";

    auto name_of = [korean](const std::string& key) -> std::string {
        if (key == "layer_height") return korean ? "레이어 높이" : "layer height";
        if (key == "sparse_infill_density") return korean ? "내부 채움" : "infill";
        if (key == "enable_support") return korean ? "서포트" : "supports";
        if (key == "brim_width" || key == "brim_type") return korean ? "브림" : "brim";
        if (key == "wall_loops") return korean ? "외벽 수" : "wall loops";
        if (key == "nozzle_temperature") return korean ? "노즐 온도" : "nozzle temperature";
        if (key == "bed_temperature") return korean ? "베드 온도" : "bed temperature";
        if (key == "outer_wall_speed") return korean ? "외벽 속도" : "outer wall speed";
        if (key == "retraction_length") return korean ? "리트랙션" : "retraction";
        return {};
    };

    std::string joined;
    int         count = 0;
    for (auto it = action["options"].begin(); it != action["options"].end() && count < 3; ++it, ++count) {
        std::string name = name_of(it.key());
        if (name.empty())
            name = it.key();
        if (it.value().is_number())
            name += korean ? " " + it.value().dump() : " to " + it.value().dump();
        else if (it.value().is_string())
            name += korean ? " " + it.value().get<std::string>() : " to " + it.value().get<std::string>();
        else if (it.value().is_boolean())
            name += it.value().get<bool>() ? (korean ? " 켜기" : " on") : (korean ? " 끄기" : " off");
        if (!joined.empty())
            joined += ", ";
        joined += name;
    }
    return korean ? joined + "을(를) 바꿉니다." : "Changing " + joined + ".";
}

std::string activity_line_for_action(const nlohmann::json& action, bool korean)
{
    const std::string type = action.value("type", "");
    if (type.empty())
        return {};
    if (type == "get_state")
        return korean ? "슬라이서 상태를 살펴봅니다." : "Looking at the slicer state.";
    if (type == "list_objects")
        return korean ? "플레이트에 있는 모델을 확인합니다." : "Checking the models on the plate.";
    if (type == "select_object")
        return korean ? "작업할 모델을 선택합니다." : "Selecting the model to work on.";
    if (type == "rotate") {
        const double z = action.value("z", action.value("angle", 0.0));
        if (std::abs(z) > 0.01)
            return korean ? (boost::format("모델을 %1%도 돌립니다.") % z).str()
                          : (boost::format("Rotating the model %1% degrees.") % z).str();
        return korean ? "모델을 돌립니다." : "Rotating the model.";
    }
    if (type == "scale")
        return korean ? "모델 크기를 바꿉니다." : "Resizing the model.";
    if (type == "translate")
        return korean ? "모델 위치를 옮깁니다." : "Moving the model.";
    if (type == "find_setting") {
        const std::string query = action.value("query", action.value("name", ""));
        if (query.empty())
            return korean ? "설정 이름을 찾습니다." : "Looking up the setting name.";
        return korean ? "설정에서 \"" + query + "\"을(를) 찾습니다."
                      : "Looking up the setting \"" + query + "\".";
    }
    if (type == "printer_diagnostics")
        return korean ? "프린터 오류 기록을 확인합니다." : "Checking the printer's error state.";
    if (type == "wiki_search") {
        const std::string query = action.value("query", "");
        if (query.empty())
            return korean ? "도움말을 검색합니다." : "Searching the help pages.";
        return korean ? "도움말에서 \"" + query + "\"을(를) 찾습니다."
                      : "Searching help for \"" + query + "\".";
    }
    if (type == "set_config")
        return setting_activity_phrase(action, korean);
    const std::string desc = describe_action_type(type, action, korean);
    if (desc.empty())
        return {};
    return korean ? desc + "을(를) 실행합니다." : "Running: " + desc + ".";
}

} // namespace

std::string ollama_thinking_goal_intro(const std::string& user_goal, bool korean)
{
    std::string goal = user_goal;
    boost::algorithm::trim(goal);
    if (goal.size() > 80)
        goal = goal.substr(0, 80) + "…";
    if (goal.empty())
        return korean ? "요청을 읽었습니다." : "Read the request.";
    return korean ? "요청: " + goal : "Request: " + goal;
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
    return korean ? "슬라이서 상태를 확인하고 변경안을 준비하고 있습니다…"
                  : "Checking slicer state and preparing changes…";
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
            add(korean ? "이 단계는 적용되지 않았습니다. 다른 방법을 시도합니다."
                       : "That step did not apply. Trying another way.");
            continue;
        }
        if (!r.value("changed", false)) {
            if (tool == "set_config")
                add(korean ? "그 설정은 이미 같은 값이었습니다." : "That setting was already at the requested value.");
            continue;
        }
        add(friendly_tool_line(tool, r.value("message", ""), korean));
    }
    return out;
}

}} // namespace
