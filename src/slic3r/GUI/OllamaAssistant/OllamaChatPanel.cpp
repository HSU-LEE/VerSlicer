#include "OllamaChatPanel.hpp"
#include "OllamaActionCritic.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaActionPipeline.hpp"
#include "BambuLabWikiSearch.hpp"
#include "OllamaDiagnosticPipeline.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaModelPick.hpp"
#include "OllamaServerManager.hpp"
#include "OllamaActionWorkflow.hpp"
#include "OllamaAgentGoalPlanner.hpp"
#include "OllamaAgentStateService.hpp"
#include "OllamaAssistRouter.hpp"
#include "OllamaConfig.hpp"
#include "OllamaExecutionPolicy.hpp"
#include "OllamaProcessingNotice.hpp"
#include "OllamaSettingDescriptions.hpp"
#include "OllamaSettingSearch.hpp"
#include "OllamaToolResult.hpp"
#include "OllamaPrintingTips.hpp"
#include "OllamaRequestRouter.hpp"
#include "OllamaResponseNormalizer.hpp"
#include "OllamaTelemetry.hpp"
#include "OllamaUserFlow.hpp"
#include "OllamaVoiceTranscript.hpp"
#include "../MakerWorld/MakerWorldImportFlow.hpp"
#include "../MakerWorld/MakerWorldSearchService.hpp"
#include "../MakerWorld/MakerWorldTypes.hpp"
#include "../MakerWorld/MakerWorldUrl.hpp"

#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../BambuSmartPrint/PrintPlannerGui.hpp"
#include "libslic3r/BambuSmartPrint/PrintGoalParser.hpp"
#include "libslic3r/BambuSmartPrint/PrintGoalSession.hpp"
#include "libslic3r/BambuSmartPrint/PrintPlanner.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/TextInput.hpp"

#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/filefn.h>
#include <wx/settings.h>
#include <wx/weakref.h>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <regex>
#include <thread>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kAssistantModeKey = "assistant_mode";
constexpr const char* kModeApply         = "apply";
constexpr const char* kModeAssist        = "assist";
constexpr const char* kModeQuestion      = "question";
constexpr int         kMaxModelPollFailures = 12;
constexpr size_t      kMaxHistoryChars      = 48000;
constexpr size_t      kMaxContextChars      = 8000;

std::string extract_first_url_from_text(const std::string& text)
{
    static const std::regex link_re(R"((https?://[^\s]+))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, link_re))
        return m[1].str();
    return {};
}

std::string last_user_request_text(const std::vector<OllamaMessage>& messages)
{
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role != "user")
            continue;
        const std::string& s = it->content;
        const std::string marker = "\n\nUser request:\n";
        const auto pos = s.rfind(marker);
        if (pos != std::string::npos)
            return s.substr(pos + marker.size());
        return s;
    }
    return {};
}

static bool message_looks_like_manual_instruction(const std::string& msg)
{
    static const char* needles[] = {
        "단축키", "shortcut", "use the", "use '", "설정하세요", "사용하세요", "사용하여",
        "brim_width", "enable_brim", "set_config", "objetos", "press ", "click ",
        "키를", "manual", "직접", "설정하려면", "pressure", "프레셔", "어드밴", "어드밋",
        "input_shap", "input_shell", "사용하여", "사용하세요",
    };
    for (const char* n : needles) {
        if (msg.find(n) != std::string::npos)
            return true;
    }
    return false;
}

static bool looks_like_json_parse_error(const std::string& what)
{
    return what.find("parse") != std::string::npos || what.find("JSON") != std::string::npos
        || what.find("json.exception") != std::string::npos
        || what.find("balanced JSON") != std::string::npos;
}

static bool ui_is_korean()
{
    return wxGetApp().current_language_code().StartsWith("ko");
}

static wxString format_chat_error(const std::string& raw)
{
    const bool ko = ui_is_korean();
    if (raw.find("Connection refused") != std::string::npos || raw.find("connection refused") != std::string::npos
        || raw.find("Could not connect") != std::string::npos || raw.find("Failed to connect") != std::string::npos)
        return ko ? wxString::FromUTF8("AI에 연결할 수 없습니다. Ollama가 실행 중인지 확인해 주세요.")
                  : _L("Can't reach the AI. Make sure Ollama is running.");

    if (raw.find("HTTP 404") != std::string::npos)
        return ko ? wxString::FromUTF8("AI 모델을 찾지 못했습니다. 모델 이름을 확인하거나 다운로드해 주세요.")
                  : _L("AI model not found. Check the model name or download it first.");

    if (raw.find("timeout") != std::string::npos || raw.find("Timeout") != std::string::npos)
        return ko ? wxString::FromUTF8("응답 시간이 초과되었습니다. 잠시 후 다시 시도해 주세요.")
                  : _L("Request timed out. Try again in a moment.");

    if (raw.find("Empty Ollama reply") != std::string::npos || raw.find("Empty HTTP body") != std::string::npos
        || raw == "Empty assistant response")
        return ko ? wxString::FromUTF8("빈 응답이 왔습니다. 다시 보내 보세요.")
                  : _L("Got an empty reply. Try sending again.");

    if (raw.find("HTTP") != std::string::npos || raw.find("curl") != std::string::npos)
        return ko ? wxString::FromUTF8("AI 서버와 통신 중 문제가 발생했습니다. Ollama 상태를 확인해 주세요.")
                  : _L("Couldn't talk to the AI server. Check that Ollama is running.");

    return wxString::FromUTF8(raw);
}

static wxString format_assistant_failure(const std::string& what, const std::string& assistant_text, bool apply_mode)
{
    if (what == "Empty assistant response")
        return format_chat_error(what);

    const bool parse_error = looks_like_json_parse_error(what);
    if (!assistant_text.empty()) {
        wxString display = wxString::FromUTF8(assistant_text);
        if (display.Length() > 600)
            display = display.Left(600) + "\n…";
        if (parse_error) {
            display += "\n\n"
                + (ui_is_korean()
                       ? wxString::FromUTF8("설명은 위와 같습니다. 자동 적용은 되지 않았어요 — '브림 켜 줘'처럼 다시 요청해 보세요.")
                       : _L("See above for the explanation. To apply automatically, try a direct request like “turn on brim”."));
        } else if (apply_mode) {
            display += "\n\n"
                + (ui_is_korean()
                       ? wxString::FromUTF8("위 내용은 참고용입니다. 자동 적용이 되지 않았어요 — 플레이트에서 모델을 선택한 뒤 다시 시도해 주세요.")
                       : _L("See above for context. It wasn't applied automatically — select a model on the plate and try again."));
        } else {
            display += "\n\n"
                + (ui_is_korean()
                       ? wxString::FromUTF8("답변 중 일부를 처리하지 못했습니다. 질문을 다시 보내 보세요.")
                       : _L("Part of the reply couldn't be processed. Try sending your question again."));
        }
        return display;
    }

    if (parse_error)
        return ui_is_korean()
            ? wxString::FromUTF8("응답을 이해하지 못했습니다. '브림 켜 줘'처럼 짧게 다시 보내 보세요.")
            : _L("Couldn't understand the reply. Try a short request like “turn on brim”.");

    const wxString mapped = format_chat_error(what);
    if (mapped != wxString::FromUTF8(what))
        return mapped;

    return ui_is_korean() ? wxString::FromUTF8("문제가 발생했습니다. 잠시 후 다시 시도해 주세요.")
                          : _L("Something went wrong. Try again in a moment.");
}

static wxString change_not_applied_msg()
{
    return ui_is_korean()
        ? wxString::FromUTF8("아직 적용되지 않았습니다. 플레이트에서 모델을 선택하거나, '브림 켜 줘'처럼 구체적으로 말씀해 주세요.")
        : _L("That wasn't applied yet. Select a model on the plate, or try a clearer request like “turn on brim”.");
}

static wxString cancelled_no_changes_msg()
{
    return ui_is_korean() ? wxString::FromUTF8("취소되었습니다 — 변경 사항은 없습니다.")
                          : _L("Cancelled — nothing was changed.");
}

static wxString preview_only_msg()
{
    return ui_is_korean()
        ? wxString::FromUTF8("미리보기만 표시되었습니다. 비교 창을 닫은 뒤 적용 여부를 확인해 주세요.")
        : _L("Preview only — close the compare dialog to finish or cancel.");
}

static wxString nothing_applied_msg()
{
    return ui_is_korean()
        ? wxString::FromUTF8("적용된 변경이 없습니다. 플레이트에서 모델을 선택한 뒤 다시 시도해 주세요.")
        : _L("Nothing was applied. Select a model on the plate and try again.");
}

static wxString manual_instruction_msg()
{
    return ui_is_korean()
        ? wxString::FromUTF8("자동으로 적용하지 못했습니다. 플레이트에서 모델을 선택하거나, 적용 모드에서 더 구체적으로 요청해 주세요.")
        : _L("Couldn't apply that automatically. Select a model on the plate, or try a more specific request in Apply mode.");
}

static wxString question_mode_actions_skipped_msg()
{
    return ui_is_korean() ? wxString::FromUTF8("(질문 모드 — 제안된 변경은 적용하지 않았습니다.)")
                          : _L("(Question mode — suggested changes were not applied.)");
}

static bool result_message_looks_jargon(const std::string& m)
{
    if (m.empty())
        return false;
    static const char* needles[] = {
        "set_config", "enable_", "sparse_", "outer_wall", "retraction_", "brim_width", "wall_loops",
        "layer_height", "preset", "options", "effective_change", "Applied mesh operation",
    };
    for (const char* n : needles) {
        if (m.find(n) != std::string::npos)
            return true;
    }
    return m.find('_') != std::string::npos && m.find(' ') == std::string::npos;
}

static wxString thinking_diagnosis_lines(const OllamaDiagnosis& diagnosis, bool ko)
{
    wxString out;
    if (!diagnosis.symptom.empty())
        out += (ko ? wxString::FromUTF8("  증상: ") : wxString("  What you reported: "))
             + wxString::FromUTF8(diagnosis.symptom) + "\n";
    if (!diagnosis.diagnosis.empty())
        out += (ko ? wxString::FromUTF8("  원인 추정: ") : wxString("  Likely cause: "))
             + wxString::FromUTF8(diagnosis.diagnosis) + "\n";
    if (!diagnosis.user_message.empty())
        out += wxString("  ") + wxString::FromUTF8(diagnosis.user_message) + "\n";
    return out;
}

static wxString thinking_wiki_lines(const nlohmann::json& wiki, bool ko)
{
    wxString out;
    if (!wiki.is_array())
        return out;
    for (const auto& item : wiki) {
        if (!item.is_object() || !item.contains("title") || !item["title"].is_string())
            continue;
        const wxString title = wxString::FromUTF8(item["title"].get<std::string>());
        out += (ko ? wxString::FromUTF8("  참고 자료: ") : wxString("  Reference: ")) + title + "\n";
    }
    return out;
}

static wxString thinking_settings_lines(const nlohmann::json& analysis, bool ko)
{
    wxString out;
    if (!analysis.contains("relevant_settings") || !analysis["relevant_settings"].is_array())
        return out;
    for (const auto& row : analysis["relevant_settings"]) {
        if (!row.is_object() || !row.contains("key"))
            continue;
        const std::string key_raw = row.value("key", "");
        const wxString    label   = wxString::FromUTF8(OllamaSettingDescriptions::setting_label(key_raw, ko));
        wxString          cur;
        if (row.contains("current") && row["current"].is_string())
            cur = wxString::FromUTF8(OllamaSettingDescriptions::format_value(
                key_raw, row["current"].get<std::string>(), ko));
        else if (row.contains("current") && !row["current"].is_null())
            cur = wxString::FromUTF8(row["current"].dump());
        const wxString note = row.contains("assessment") && row["assessment"].is_string()
            ? wxString::FromUTF8(row["assessment"].get<std::string>())
            : wxString{};
        if (cur.IsEmpty())
            out += wxString::Format("  %s — %s\n", label, note);
        else
            out += wxString::Format("  %s: %s — %s\n", label, cur, note);
    }
    return out;
}

static wxString strip_leading_list_numbering(const wxString& line)
{
    wxString s = line;
    s          = s.Trim();
    size_t i   = 0;
    while (i < s.Length() && wxIsdigit(s[i]))
        ++i;
    if (i > 0 && i < s.Length() && (s[i] == '.' || s[i] == ')') && i + 1 < s.Length())
        return s.Mid(static_cast<long>(i + 1)).Trim();
    return s;
}

static wxString format_numbered_summary_lines(const std::vector<wxString>& lines)
{
    wxString out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i)
            out += "\n";
        out += wxString::Format("%zu. %s", i + 1, strip_leading_list_numbering(lines[i]));
    }
    return out;
}

static bool workflow_had_effective_change(const std::vector<OllamaActionResult>& results)
{
    for (const auto& r : results) {
        if (r.success && r.effective_change && !OllamaUserFlow::result_is_navigation_only(r))
            return true;
    }
    return false;
}

static wxString summarize_applied_changes(const std::string& /*user_req*/,
                                          const std::vector<OllamaActionResult>& results)
{
    auto had_effective_change = [&]() { return workflow_had_effective_change(results); };
    auto had_noop_only = [&]() {
        bool saw_success = false;
        for (const auto& r : results) {
            if (!r.success)
                continue;
            saw_success = true;
            if (r.effective_change && !OllamaUserFlow::result_is_navigation_only(r))
                return false;
        }
        return saw_success;
    };
    const bool ko = ui_is_korean();
    if (!had_effective_change()) {
        if (had_noop_only())
            return ko ? wxString::FromUTF8("요청하신 값과 동일해서 변경하지 않았습니다.")
                      : _L("Settings were already at the requested values — nothing changed.");
        return ko ? wxString::FromUTF8("적용되지 않았습니다. 플레이트에서 모델을 선택한 뒤 다시 시도해 주세요.")
                  : _L("Nothing was applied. Select a model on the plate and try again.");
    }

    std::vector<wxString> lines;
    auto add_once = [&](const wxString& line) {
        for (const auto& existing : lines)
            if (existing == line)
                return;
        lines.push_back(line);
    };

    for (const auto& r : results) {
        if (!r.success || !r.effective_change || OllamaUserFlow::result_is_navigation_only(r))
            continue;
        const std::string& m = r.message;
        if (m.find("brim") != std::string::npos || m.find("Brim") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("브림을 켰습니다 — 바닥 접착을 돕습니다.")
                        : _L("Enabled brim — extra plastic around the bottom for better adhesion."));
        else if (m.find("enable_support") != std::string::npos || m.find("Supports") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("트리 서포트를 켰습니다.")
                        : _L("Enabled tree supports for overhanging parts."));
        else if (m.find("outer_wall_speed") != std::string::npos || m.find("sparse_infill_speed") != std::string::npos
                 || m.find("speed") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("인쇄 속도를 조정했습니다.") : _L("Adjusted print speed."));
        else if (m.find("sparse_infill") != std::string::npos || m.find("wall_loops") != std::string::npos
                 || m.find("Infill") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("채움과 벽을 조정해 더 단단하게 만들었습니다.")
                        : _L("Adjusted infill and walls to make the part stronger."));
        else if (m.find("brim_width=0") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("브림을 껐습니다.") : _L("Turned brim off."));
        else if (m.find("Rotated") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("선택한 모델을 회전했습니다.") : _L("Rotated the selected model."));
        else if (m.find("Scaled selection") != std::string::npos || m.find("Scaled") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("모델 크기를 조정했습니다.") : _L("Resized the model."));
        else if (m.find("Auto-arrange") != std::string::npos || m.find("Arranged models") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("빌드 플레이트에서 모델을 재배치했습니다.")
                        : _L("Re-arranged models on the build plate."));
        else if (m.find("by-object") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("객체 단위로 재배치했습니다.") : _L("Arranged models by object."));
        else if (m.find("Split model") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("모델을 개별 객체로 분할했습니다.") : _L("Split the model into separate objects."));
        else if (m.find("Added plate") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("새 빌드 플레이트를 추가했습니다.") : _L("Added a new build plate."));
        else if (m.find("Applied mesh operation") != std::string::npos
                 || m.find("subtract_cylinder") != std::string::npos) {
            if (m.find("subtract") != std::string::npos || m.find("drill") != std::string::npos)
                add_once(ko ? wxString::FromUTF8("모델에 구멍을 뚫었습니다.") : _L("Drilled a hole in the model."));
            else if (m.find("add_handle") != std::string::npos || m.find("handle") != std::string::npos)
                add_once(ko ? wxString::FromUTF8("손잡이를 추가했습니다.") : _L("Added a handle to the model."));
            else if (m.find("add_rib") != std::string::npos || m.find("rib") != std::string::npos)
                add_once(ko ? wxString::FromUTF8("리브(보강)를 추가했습니다.") : _L("Added reinforcement ribs."));
            else
                add_once(ko ? wxString::FromUTF8("모델 형상을 수정했습니다.") : _L("Modified model geometry."));
        } else if (m.find("Repaired mesh") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("메쉬를 수리했습니다.") : _L("Repaired the mesh."));
        else if (m.find("Mirrored mesh") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("모델을 대칭으로 만들었습니다.") : _L("Mirrored the model."));
        else if (m.find("slicing") != std::string::npos || m.find("Slicing") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("현재 플레이트 슬라이싱을 시작했습니다.")
                        : _L("Started slicing the current plate."));
        else if (m.find("retraction") != std::string::npos || m.find("stringing") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("실처럼 늘어지는 현상을 줄였습니다.")
                        : _L("Adjusted settings to reduce stringing."));
        else if (m.find("layer_height") != std::string::npos || m.find("layer height") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("층 두께를 조정했습니다.") : _L("Adjusted layer height."));
        else if (m.find("temperature") != std::string::npos || m.find("Temperature") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("노즐·베드 온도를 조정했습니다.") : _L("Adjusted print temperature."));
        else if (m.find("Translated") != std::string::npos || m.find("translate") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("모델 위치를 옮겼습니다.") : _L("Moved the model."));
        else if (m.find("Deleted") != std::string::npos || m.find("delete") != std::string::npos)
            add_once(ko ? wxString::FromUTF8("선택한 모델을 삭제했습니다.") : _L("Deleted the selected model."));
        else if (!m.empty() && !result_message_looks_jargon(m))
            add_once(strip_leading_list_numbering(wxString::FromUTF8(m)));
        else if (!m.empty())
            add_once(ko ? wxString::FromUTF8("인쇄 설정을 업데이트했습니다.") : _L("Print settings updated."));
    }

    if (lines.empty())
        add_once(ko ? wxString::FromUTF8("인쇄 설정을 업데이트했습니다.") : _L("Print settings updated."));

    return format_numbered_summary_lines(lines);
}

static std::string assistant_history_content(const std::string& assistant_text, bool apply_mode)
{
    if (apply_mode)
        return assistant_text;
    try {
        nlohmann::json hist = OllamaActionPipeline::extract_from_assistant_text(assistant_text);
        OllamaActionPipeline::strip_actions_for_question_history(hist);
        if (hist.contains("message") && hist["message"].is_string())
            return hist["message"].get<std::string>();
    } catch (...) {
    }
    return std::string("(assistant reply — actions omitted from history)");
}

static void push_assistant_history_stub(std::vector<OllamaMessage>& messages, const std::string& stub)
{
    messages.push_back({"assistant", stub});
}

static std::string parse_failure_history_stub(const std::string& err_what)
{
    if (err_what.find("unexpected end of input") != std::string::npos
        || err_what.find("Empty assistant response") != std::string::npos)
        return "(assistant reply could not be parsed — empty or incomplete JSON omitted from history)";
    return std::string("(assistant reply could not be parsed — omitted from history: ") + err_what + ")";
}
static void drop_last_assistant_message(std::vector<OllamaMessage>& messages)
{
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == "assistant") {
            messages.erase(std::next(it).base());
            return;
        }
    }
}

static wxString last_assistant_message(const wxString& chat)
{
    const wxString marker = _L("Assistant") + wxT("\n");
    wxString       last_line;
    int            offset = 0;
    while (true) {
        const int p = chat.Mid(offset).Find(marker);
        if (p == wxNOT_FOUND)
            break;
        const int abs = offset + p + static_cast<int>(marker.length());
        last_line     = chat.Mid(abs).BeforeFirst('\n').Trim();
        offset        = abs;
    }
    return last_line;
}

static wxString completion_status_for_reply(const wxString& reply, bool apply_mode)
{
    if (!apply_mode)
        return ui_is_korean() ? wxString::FromUTF8("답변 완료") : _L("Answer ready");

    if (reply.IsEmpty())
        return ui_is_korean() ? wxString::FromUTF8("완료") : _L("Done");

    if (reply.Contains(_L("No models found")) || reply.Contains(wxString::FromUTF8("모델을 찾지 못")))
        return ui_is_korean() ? wxString::FromUTF8("MakerWorld 검색 완료 — 결과 없음")
                              : _L("MakerWorld: no models found");

    if (reply.Contains(_L("Search cancelled")) || reply.Contains(_L("Import cancelled")))
        return ui_is_korean() ? wxString::FromUTF8("취소됨") : _L("Cancelled");

    if (reply.Contains(_L("Model loaded")) || reply.Contains(wxString::FromUTF8("불러왔")))
        return ui_is_korean() ? wxString::FromUTF8("모델 불러오기 완료") : _L("Model loaded");

    if (reply.Contains(_L("Import failed")) || reply.Contains(wxString::FromUTF8("가져오기 실패")))
        return ui_is_korean() ? wxString::FromUTF8("가져오기 실패") : _L("Import failed");

    if (reply.Contains(_L("Found ")) && reply.Contains(_L("models on MakerWorld")))
        return ui_is_korean() ? wxString::FromUTF8("MakerWorld 검색 완료") : _L("MakerWorld search done");

    if (reply.Contains(_L("Cancelled")) || reply.Contains(wxString::FromUTF8("취소")))
        return ui_is_korean() ? wxString::FromUTF8("취소됨") : _L("Cancelled");

    if (reply.Contains(_L("Nothing was applied")) || reply.Contains(_L("wasn't applied"))
        || reply.Contains(wxString::FromUTF8("적용되지 않")) || reply.Contains(wxString::FromUTF8("아직 적용")))
        return ui_is_korean() ? wxString::FromUTF8("적용된 변경 없음") : _L("Nothing applied");

    if (reply.Contains(wxString::FromUTF8("분할")) || reply.Contains(_L("Split"))
        || reply.Contains(wxString::FromUTF8("구멍")) || reply.Contains(_L("Drilled a hole"))
        || reply.Contains(wxString::FromUTF8("형상")) || reply.Contains(_L("Modified model"))
        || reply.Contains(wxString::FromUTF8("메쉬")) || reply.Contains(_L("Repaired the mesh"))
        || reply.Contains(wxString::FromUTF8("회전")) || reply.Contains(_L("Rotated"))
        || reply.Contains(wxString::FromUTF8("크기를 조정")) || reply.Contains(_L("Resized the model")))
        return ui_is_korean() ? wxString::FromUTF8("모델 수정 완료") : _L("Model edit done");

    if (reply.Contains(wxString::FromUTF8("브림")) || reply.Contains(wxString::FromUTF8("서포트"))
        || reply.Contains(wxString::FromUTF8("속도")) || reply.Contains(wxString::FromUTF8("켰"))
        || reply.Contains(wxString::FromUTF8("업데이트")) || reply.Contains(_L("Enabled brim"))
        || reply.Contains(_L("Print settings updated")))
        return ui_is_korean() ? wxString::FromUTF8("설정 반영 완료") : _L("Settings updated");

    if (reply.Contains(wxString::FromUTF8("크기")) || reply.Contains(_L("Resized")))
        return ui_is_korean() ? wxString::FromUTF8("모델 수정 완료") : _L("Model edit done");

    if (reply.Contains(_L("couldn't understand")) || reply.Contains(wxString::FromUTF8("이해하지 못했")))
        return ui_is_korean() ? wxString::FromUTF8("다시 입력해 주세요") : _L("Try again");

    if (reply.Contains(wxString::FromUTF8("플레이트")) || reply.Contains(_L("build plate")))
        return ui_is_korean() ? wxString::FromUTF8("플레이트 추가 완료") : _L("Plate added");

    return ui_is_korean() ? wxString::FromUTF8("완료") : _L("Done");
}

} // namespace

static bool process_makerworld_actions(nlohmann::json& root, wxWindow* parent, bool apply_mode,
                                       const std::string& user_req, MakerWorldFlowUiCallbacks callbacks)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;

    bool handled = false;
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& action : root["actions"]) {
        if (!action.is_object() || !action.contains("type") || !action["type"].is_string()) {
            kept.push_back(action);
            continue;
        }
        const std::string type = action["type"].get<std::string>();
        if (type == "makerworld_search") {
            std::string query = action.value("query", "");
            if (query.empty())
                query = MakerWorldSearchService::normalize_search_query(user_req);
            MakerWorldImportFlow::run_search_from_query(parent, query, apply_mode, callbacks);
            handled = true;
            continue;
        }
        if (type == "import_makerworld") {
            if (!apply_mode) {
                if (callbacks.append_chat)
                    callbacks.append_chat(_L("Switch to Apply mode to import a MakerWorld model."));
            } else if (action.contains("url") && action["url"].is_string()) {
                MakerWorldImportFlow::run_import_from_user_text(parent, action["url"].get<std::string>(), true,
                                                                callbacks);
            } else {
                MakerWorldCandidate c;
                c.design_id = action.value("design_id", "");
                c.title     = action.value("title", "model_" + c.design_id);
                if (!c.design_id.empty())
                    MakerWorldImportFlow::confirm_and_import(parent, c, true, callbacks);
            }
            handled = true;
            continue;
        }
        kept.push_back(action);
    }
    root["actions"] = kept;
    return handled;
}

OllamaChatPanel::OllamaChatPanel(wxWindow* parent, bool show_header)
    : wxPanel(parent, wxID_ANY)
    , m_client(kOllamaDefaultHost)
    , m_alive(std::make_shared<std::atomic<bool>>(true))
    , m_show_header(show_header)
{
    SlicePilotUi::apply_panel_chrome(this);

    auto* topsizer = new wxBoxSizer(wxVERTICAL);

    // Header (small, chat-like).
    if (m_show_header) {
        m_header = new wxPanel(this);
        m_header->SetBackgroundColour(SlicePilotUi::Theme::surface_alt());
        auto* header_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_collapse_btn     = new wxButton(m_header, wxID_ANY, "–", wxDefaultPosition, wxSize(FromDIP(26), FromDIP(22)));
        m_title            = new wxStaticText(m_header, wxID_ANY,
            wxGetApp().current_language_code().StartsWith("ko") ? wxString::FromUTF8("AI 도우미")
                                                                : _L("AI Assistant"));
        m_title->SetForegroundColour(SlicePilotUi::Theme::text());
        header_sizer->Add(m_collapse_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        header_sizer->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
        m_header->SetSizer(header_sizer);
        topsizer->Add(m_header, 0, wxEXPAND);
    }

    // Body — match Verslicer dialog margins and flat panels
    m_body = new wxPanel(this);
    m_body->SetBackgroundColour(SlicePilotUi::Theme::background());
    auto* body_sizer = new wxBoxSizer(wxVERTICAL);
    const int pad       = SlicePilotUi::content_side_margin_dip(m_body, false);
    const int gap       = FromDIP(8);
    const int row_h     = FromDIP(26);
    const int compose_h = FromDIP(32);

    // Toolbar: model | mode | reset
    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    auto* model_key = new wxStaticText(m_body, wxID_ANY, _L("Model"));
    model_key->SetForegroundColour(SlicePilotUi::Theme::text_muted());
    model_key->SetFont(Label::Body_13);
    m_model_label = new wxStaticText(m_body, wxID_ANY, wxString::FromUTF8(kOllamaDefaultModel));
    m_model_label->SetForegroundColour(SlicePilotUi::Theme::text());
    m_model_label->SetFont(Label::Body_13);
    toolbar->Add(model_key, 0, wxALIGN_CENTER_VERTICAL);
    toolbar->Add(m_model_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

    toolbar->AddStretchSpacer(1);

    m_mode_label = new wxStaticText(m_body, wxID_ANY, _L("Mode"));
    m_mode_label->SetForegroundColour(SlicePilotUi::Theme::text_muted());
    m_mode_label->SetFont(Label::Body_13);
    m_mode_choice = new wxChoice(m_body, wxID_ANY);
    m_mode_choice->Append(_L("Question"));
    m_mode_choice->Append(_L("Assist"));
    m_mode_choice->SetMinSize(wxSize(FromDIP(96), row_h));
    m_mode_choice->SetFont(Label::Body_13);
    m_mode_choice->SetBackgroundColour(SlicePilotUi::Theme::background());
    m_mode_choice->SetForegroundColour(SlicePilotUi::Theme::text());
    wxGetApp().UpdateDarkUI(m_mode_choice);

    m_reset_btn = new Button(m_body, _L("Reset"));
    SlicePilotUi::style_secondary_button(m_reset_btn);
    m_reset_btn->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    SlicePilotUi::size_compact_toolbar_button(m_body, m_reset_btn);

    toolbar->Add(m_mode_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, gap);
    toolbar->Add(m_mode_choice, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
    toolbar->Add(m_reset_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    body_sizer->Add(toolbar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    auto* toolbar_rule = new wxPanel(m_body, wxID_ANY, wxDefaultPosition, wxSize(-1, std::max(1, FromDIP(1))));
    toolbar_rule->SetBackgroundColour(SlicePilotUi::Theme::border());
    body_sizer->Add(toolbar_rule, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

    // Chat log (flat — same background as window)
    m_history_ctrl = new wxTextCtrl(m_body, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP | wxTE_RICH2 | wxBORDER_NONE);
    m_history_ctrl->SetBackgroundColour(SlicePilotUi::Theme::background());
    m_history_ctrl->SetForegroundColour(SlicePilotUi::Theme::text());
    m_history_ctrl->SetFont(Label::Body_14);
    body_sizer->Add(m_history_ctrl, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

    // Footer: input row + status (aligned under input, vertically centered)
    auto* footer_block = new wxBoxSizer(wxVERTICAL);
    auto* footer_row = new wxBoxSizer(wxHORIZONTAL);

    m_input_field = new TextInput(m_body, wxEmptyString, wxEmptyString, wxEmptyString,
                                  wxDefaultPosition, wxSize(-1, compose_h), wxTE_PROCESS_ENTER);
    m_input_field->SetCornerRadius(FromDIP(6));
    m_input_field->SetMinSize(wxSize(-1, compose_h));
    m_input_ctrl = m_input_field->GetTextCtrl();
    m_input_ctrl->SetHint(wxEmptyString);
    wxGetApp().UpdateDarkUI(m_input_field);
    wxGetApp().UpdateDarkUI(m_input_ctrl);

    footer_row->Add(m_input_field, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

    m_send_btn = new Button(m_body, _L("Send"));
    SlicePilotUi::style_primary_button(m_send_btn);
    m_send_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Compact);
    m_send_btn->SetPaddingSize(wxSize(FromDIP(14), FromDIP(6)));
    const int send_w = FromDIP(62);
    m_send_btn->SetMinSize(wxSize(send_w, compose_h));
    m_send_btn->SetMaxSize(wxSize(send_w, compose_h));
    footer_row->Add(m_send_btn, 0, wxALIGN_CENTER_VERTICAL);

    footer_block->Add(footer_row, 0, wxEXPAND);

    m_status_host = new wxPanel(m_body, wxID_ANY);
    m_status_host->SetBackgroundColour(SlicePilotUi::Theme::background());
    auto* status_row = new wxBoxSizer(wxHORIZONTAL);
    m_status = new wxStaticText(m_status_host, wxID_ANY,
                                ui_is_korean() ? wxString::FromUTF8("준비됨") : _L("Ready"));
    m_status->SetForegroundColour(SlicePilotUi::Theme::text_muted());
    m_status->SetFont(Label::Body_13);
    status_row->Add(m_status, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    m_status_host->SetSizer(status_row);

    footer_block->Add(m_status_host, 0, wxEXPAND | wxTOP, FromDIP(6));
    body_sizer->Add(footer_block, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);

    wxGetApp().UpdateDarkUI(m_history_ctrl);

    m_body->SetSizer(body_sizer);
    topsizer->Add(m_body, 1, wxEXPAND);

    SetSizer(topsizer);

    m_send_btn->Bind(wxEVT_BUTTON, &OllamaChatPanel::on_send, this);
    m_input_ctrl->Bind(wxEVT_TEXT_ENTER, &OllamaChatPanel::on_send, this);
    if (m_collapse_btn)
        m_collapse_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { set_collapsed(!m_collapsed); });

    m_mode_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent& e) {
        m_apply_mode = e.GetSelection() != 0;
        save_settings();
        refresh_mode_ui();
        if (!m_messages.empty())
            m_messages.front() = {"system", OllamaActionExecutor::build_system_prompt(m_apply_mode)};
        update_system_welcome_in_chat();
        e.Skip();
    });
    m_reset_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { reset_conversation(); });

    load_settings();
    reset_conversation();
    set_status_text(ui_is_korean() ? wxString::FromUTF8("준비됨") : _L("Ready"));

    m_poll_timer = new wxTimer(this);
    m_poll_timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (!m_alive->load() || wxGetApp().is_closing())
            return;
        refresh_models();
    });

    ensure_ollama_running();
}

OllamaChatPanel::~OllamaChatPanel()
{
    m_alive->store(false);
    ++m_request_gen;
    OllamaClient::cancel_active_requests(OllamaCancelDomain::Chat);
    if (m_assist_controller)
        m_assist_controller.reset();
    if (m_poll_timer) {
        m_poll_timer->Stop();
        m_poll_timer = nullptr;
    }
    if (!wxTheApp || !wxGetApp().is_closing())
        OllamaProcessingNotice::hide(wxGetApp().plater());
    SetEvtHandlerEnabled(false);
}

void OllamaChatPanel::schedule_model_poll(int delay_ms)
{
    if (!m_poll_timer || !m_alive->load())
        return;
    m_poll_timer->StartOnce(delay_ms);
}

void OllamaChatPanel::trim_message_history()
{
    constexpr size_t kMaxTurns = 16;
    if (m_messages.size() <= 1)
        return;
    const size_t keep = 1 + kMaxTurns * 2;
    if (m_messages.size() <= keep)
        return;
    std::vector<OllamaMessage> trimmed;
    trimmed.reserve(keep);
    trimmed.push_back(m_messages.front());
    trimmed.insert(trimmed.end(), m_messages.end() - (keep - 1), m_messages.end());
    m_messages.swap(trimmed);
}

void OllamaChatPanel::trim_history_display()
{
    if (m_persistent_chat.length() <= kMaxHistoryChars)
        return;
    m_persistent_chat = m_persistent_chat.Mid(m_persistent_chat.length() - static_cast<int>(kMaxHistoryChars));
    refresh_chat_display();
}

wxString OllamaChatPanel::thinking_role_label() const
{
    return ui_is_korean() ? wxString::FromUTF8("처리 중…") : _L("Working…");
}

void OllamaChatPanel::refresh_chat_display()
{
    if (!m_history_ctrl)
        return;
    const wxString display = m_persistent_chat + m_thinking_block;
    m_history_ctrl->ChangeValue(display);

    const long persist_len = static_cast<long>(m_persistent_chat.length());
    const long total_len   = static_cast<long>(display.length());
    const wxTextAttr normal_attr(SlicePilotUi::Theme::text(), wxNullColour, m_history_ctrl->GetFont());
    if (persist_len > 0)
        m_history_ctrl->SetStyle(0, persist_len, normal_attr);
    if (!m_thinking_block.IsEmpty() && total_len > persist_len) {
        const wxTextAttr muted_attr(SlicePilotUi::Theme::text_muted(), wxNullColour, m_history_ctrl->GetFont());
        m_history_ctrl->SetStyle(persist_len, total_len, muted_attr);
    }

    m_history_ctrl->ShowPosition(m_history_ctrl->GetLastPosition());
}

void OllamaChatPanel::begin_thinking_block()
{
    if (!m_thinking_block.IsEmpty())
        return;
    m_thinking_block = thinking_role_label() + "\n";
    refresh_chat_display();
}

void OllamaChatPanel::append_thinking_line(const wxString& line)
{
    if (line.IsEmpty())
        return;
    begin_thinking_block();
    m_thinking_block += line + "\n";
    refresh_chat_display();
}

void OllamaChatPanel::append_thinking_text(const wxString& text)
{
    if (text.IsEmpty())
        return;
    begin_thinking_block();
    m_thinking_block += text;
    if (!text.EndsWith("\n"))
        m_thinking_block += "\n";
    refresh_chat_display();
}

void OllamaChatPanel::clear_thinking_block()
{
    if (m_thinking_block.IsEmpty())
        return;
    m_thinking_block.Clear();
    refresh_chat_display();
}

void OllamaChatPanel::submit_text_and_send(const wxString& text)
{
    if (!m_input_ctrl)
        return;
    m_input_ctrl->SetValue(text);
    wxCommandEvent evt;
    on_send(evt);
}

void OllamaChatPanel::set_input_text(const wxString& text)
{
    if (!m_input_ctrl)
        return;
    m_input_ctrl->SetValue(text);
    m_input_ctrl->SetFocus();
}

void OllamaChatPanel::set_collapsed(bool collapsed)
{
    m_collapsed = collapsed;
    if (m_body)
        m_body->Show(!collapsed);
    if (m_collapse_btn)
        m_collapse_btn->SetLabel(collapsed ? "+" : "–");
    Layout();
    if (GetParent())
        GetParent()->Layout();
}

std::string OllamaChatPanel::resolve_installed_model(const std::vector<std::string>& models, const std::string& want) const
{
    if (models.empty())
        return normalize_ollama_model_tag(want);
    return pick_installed_ollama_model(models, want);
}

void OllamaChatPanel::update_model_label_ui()
{
    if (m_model_label)
        m_model_label->SetLabel(wxString::FromUTF8(m_model));
}

void OllamaChatPanel::load_settings()
{
    m_client.set_base_url(ollama_host_from_config());
    m_model = ollama_model_from_config();
    if (!wxGetApp().app_config) {
        update_model_label_ui();
        return;
    }
    const std::string saved_model = wxGetApp().app_config->get(kOllamaConfigSection, kOllamaModelKey);
    if (!saved_model.empty())
        m_model = normalize_ollama_model_tag(saved_model);
    if (!m_available_models.empty())
        m_model = resolve_installed_model(m_available_models, m_model);
    update_model_label_ui();
    const std::string mode = wxGetApp().app_config->get(kOllamaConfigSection, kAssistantModeKey);
    if (mode == kModeQuestion || mode == kAssistantModeQuestion)
        m_apply_mode = false;
    else
        m_apply_mode = true;
    if (m_mode_choice)
        m_mode_choice->SetSelection(m_apply_mode ? 1 : 0);
    refresh_mode_ui();
}

void OllamaChatPanel::save_settings()
{
    if (!wxGetApp().app_config)
        return;
    wxGetApp().app_config->set(kOllamaConfigSection, kOllamaModelKey, normalize_ollama_model_tag(m_model));
    const char* mode_str = m_apply_mode ? kModeAssist : kModeQuestion;
    wxGetApp().app_config->set(kOllamaConfigSection, kAssistantModeKey, mode_str);
    wxGetApp().app_config->save();
}

wxString OllamaChatPanel::system_welcome_message() const
{
    if (m_apply_mode) {
        return ui_is_korean()
            ? wxString::FromUTF8("적용 모드입니다. 회전·크기 조정, 구멍 뚫기, 들뜸·수축 줄이기처럼 원하는 결과를 말씀해 주세요.")
            : _L("Apply mode: say what you want — rotate, resize, drill a hole, fix warping, and more.");
    }
    return ui_is_korean()
        ? wxString::FromUTF8("질문 모드입니다. 인쇄·설정에 대해 물어보세요. 프로그램 설정은 바꾸지 않습니다.")
        : _L("Question mode: ask anything about printing. Nothing will be changed — you'll get a plain-language explanation.");
}

void OllamaChatPanel::refresh_mode_ui()
{
    if (m_mode_choice) {
        const int sel = m_apply_mode ? 1 : 0;
        if (m_mode_choice->GetSelection() != sel)
            m_mode_choice->SetSelection(sel);
    }
    if (m_input_ctrl) {
        if (m_apply_mode) {
            m_input_ctrl->SetHint(ui_is_korean()
                ? wxString::FromUTF8("예: 45도 돌려 줘 · 50%로 줄여 줘 · 중간에 구멍 뚫어 줘 · 코너 들뜸 줄여 줘")
                : _L("e.g. rotate 45°, scale to 50%, drill a hole in the center, reduce corner warping"));
        } else {
            m_input_ctrl->SetHint(ui_is_korean()
                ? wxString::FromUTF8("예: 브림이 뭐예요? · 서포트는 언제 필요해? · MakerWorld에서 드래곤 찾아줘")
                : _L("e.g. What is a brim? When do I need supports? Search MakerWorld for a dragon"));
        }
    }
}

void OllamaChatPanel::set_status_text(const wxString& text)
{
    if (!m_status)
        return;
    wxString label = text;
    if (label.Find('\n') != wxNOT_FOUND)
        label = label.BeforeFirst('\n').Trim();
    // Status is a short hint — never echo the assistant chat body below the input.
    if (label.length() > 72)
        label = label.Left(69) + wxT("…");
    if (m_status->GetLabel() == label)
        return;
    m_status->SetLabel(label);
    m_status->Wrap(0);
    if (m_status_host)
        m_status_host->Layout();
    if (m_body)
        m_body->Layout();
}

void OllamaChatPanel::set_assistant_mode(bool apply_mode)
{
    if (m_apply_mode == apply_mode)
        return;
    m_apply_mode = apply_mode;
    save_settings();
    refresh_mode_ui();
    if (!m_messages.empty())
        m_messages.front() = {"system", OllamaActionExecutor::build_system_prompt(m_apply_mode)};
    update_system_welcome_in_chat();
}

void OllamaChatPanel::update_system_welcome_in_chat()
{
    if (!m_history_ctrl)
        return;

    const wxString role    = _L("System");
    const wxString welcome = system_welcome_message();
    const wxString block   = wxString::Format("%s\n%s\n\n", role, welcome);
    const wxString prefix  = role + "\n";

    if (m_persistent_chat.StartsWith(prefix)) {
        const int body_start = static_cast<int>(prefix.length());
        const int next_block = m_persistent_chat.find("\n\n", body_start);
        if (next_block != wxNOT_FOUND)
            m_persistent_chat = block + m_persistent_chat.Mid(next_block + 2);
        else
            m_persistent_chat = block;
    } else {
        m_persistent_chat = block + m_persistent_chat;
    }
    refresh_chat_display();
}

void OllamaChatPanel::reset_conversation()
{
    ++m_request_gen;
    if (m_assist_controller && m_assist_controller->is_running())
        m_assist_controller->cancel();
    set_busy(false);
    m_messages.clear();
    m_messages.push_back({"system", OllamaActionExecutor::build_system_prompt(m_apply_mode)});
    m_persistent_chat.Clear();
    m_thinking_block.Clear();
    if (m_history_ctrl)
        m_history_ctrl->Clear();
    append_chat(_L("System"), system_welcome_message());
    set_status_text(ui_is_korean() ? wxString::FromUTF8("준비됨") : _L("Ready"));
}

void OllamaChatPanel::ensure_ollama_running()
{
    m_client.set_base_url(ollama_host_from_config());
    const auto alive = m_alive;
    wxWeakRef<wxWindow> weak(this);
    m_client.list_models([alive, weak](const std::vector<std::string>&, const std::string& error) {
        wxGetApp().CallAfter([alive, weak, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            auto* panel = dynamic_cast<OllamaChatPanel*>(weak.get());
            if (!panel)
                return;
            if (error.empty()) {
                OllamaServerManager::note_serve_reachable();
                OllamaProcessingNotice::hide(wxGetApp().plater());
                panel->set_status_text(ui_is_korean() ? wxString::FromUTF8("준비됨") : _L("Ready"));
                return;
            }

            if (OllamaServerManager::should_spawn_serve()) {
                panel->set_status_text(ui_is_korean() ? wxString::FromUTF8("AI 시작 중…") : _L("Starting AI…"));
                OllamaProcessingNotice::show_starting(wxGetApp().plater());
                OllamaServerManager::note_serve_spawn_attempt();
                const wxString cmd = OllamaServerManager::resolve_ollama_command();
                OllamaTelemetry::server_spawn_attempt(OllamaServerManager::serve_spawn_attempt_count(),
                                                        cmd.utf8_string());
                const long pid = wxExecute(cmd + " serve", wxEXEC_ASYNC);
                if (pid > 0) {
                    OllamaServerManager::mark_started(pid);
                    OllamaTelemetry::server_spawn_success(pid);
                } else {
                    OllamaTelemetry::server_spawn_failed("wxExecute failed");
                }
            }
            panel->schedule_model_poll(1200);
        });
    });
}

void OllamaChatPanel::refresh_models()
{
    if (!m_alive->load() || wxGetApp().is_closing())
        return;
    if (m_model_poll_failures >= kMaxModelPollFailures)
        return;
    if (m_status)
        set_status_text(ui_is_korean() ? wxString::FromUTF8("모델 불러오는 중…") : _L("Loading models…"));
    m_client.set_base_url(ollama_host_from_config());
    const auto alive = m_alive;
    wxWeakRef<wxWindow> weak(this);
    m_client.list_models([alive, weak](const std::vector<std::string>& models, const std::string& error) {
        wxGetApp().CallAfter([alive, weak, models, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            auto* panel = dynamic_cast<OllamaChatPanel*>(weak.get());
            if (!panel)
                return;
            panel->on_models_loaded(models, error);
        });
    });
}

void OllamaChatPanel::append_chat(const wxString& role, const wxString& text)
{
    if (!m_history_ctrl)
        return;
    m_persistent_chat += wxString::Format("%s\n%s\n\n", role, text);
    trim_history_display();
    refresh_chat_display();
}

MakerWorldFlowUiCallbacks OllamaChatPanel::makerworld_flow_callbacks()
{
    MakerWorldFlowUiCallbacks cb;
    wxWeakRef<OllamaChatPanel> weak(this);
    cb.append_chat = [weak](const wxString& msg) {
        if (auto* panel = weak.get()) {
            panel->append_chat(_L("Assistant"), msg);
            panel->m_messages.push_back({"assistant", msg.utf8_string()});
            panel->trim_message_history();
        }
    };
    cb.set_busy = [weak](bool busy, const wxString& status) {
        if (auto* panel = weak.get()) {
            panel->set_busy(busy);
            if (!status.IsEmpty())
                panel->set_status_text(status);
        }
    };
    cb.on_flow_finished = [weak]() {
        wxGetApp().CallAfter([weak]() {
            if (auto* panel = weak.get())
                panel->set_status_text(completion_status_for_reply(last_assistant_message(panel->m_persistent_chat),
                                                                    panel->m_apply_mode));
        });
    };
    return cb;
}

void OllamaChatPanel::set_busy(bool busy)
{
    m_busy = busy;
    if (m_send_btn)
        m_send_btn->Enable(!busy);
    if (m_reset_btn)
        m_reset_btn->Enable(!busy);
    if (m_mode_choice)
        m_mode_choice->Enable(!busy);
    if (m_input_field)
        m_input_field->Enable(!busy);
    Plater* plater = wxGetApp().plater();
    if (busy) {
        begin_thinking_block();
        if (m_status)
            set_status_text(ui_is_korean() ? wxString::FromUTF8("요청 처리 중…") : _L("Working on your request…"));
        OllamaProcessingNotice::show_thinking(plater);
    } else {
        OllamaProcessingNotice::hide(plater);
    }
}

void OllamaChatPanel::on_send(wxCommandEvent&)
{
    if (m_assist_controller && m_assist_controller->is_running()) {
        m_assist_controller->cancel();
        clear_thinking_block();
        set_busy(false);
    }
    if (m_busy)
        return;
    const wxString user_text = m_input_ctrl->GetValue().Trim();
    if (user_text.empty())
        return;

    save_settings();
    OllamaClient::cancel_active_requests(OllamaCancelDomain::Chat);
    m_input_ctrl->Clear();
    append_chat(_L("You"), user_text);
    m_empty_reply_retries = 0;

    const std::string user_utf8 = user_text.utf8_string();

    if (ollama_voice_looks_like_garbled_chat(user_utf8)) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        append_chat(_L("Assistant"),
                    ko ? wxString::FromUTF8("음성/문장을 이해하지 못했습니다. 다시 말씀하시거나 직접 입력해 주세요.")
                       : _L("I couldn't understand that. Try again or type your request."));
        set_status_text(completion_status_for_reply(
            ko ? wxString::FromUTF8("음성/문장을 이해하지 못했습니다. 다시 말씀하시거나 직접 입력해 주세요.")
               : _L("I couldn't understand that. Try again or type your request."),
            m_apply_mode));
        return;
    }

    if (MakerWorldSearchService::is_pure_makerworld_request(user_utf8)) {
        m_messages.push_back({"user", user_utf8});
        trim_message_history();
        const bool is_import = MakerWorldSearchService::user_wants_makerworld_import(user_utf8);
        const wxString stub  = is_import ? _L("Importing from MakerWorld…") : _L("Searching MakerWorld…");
        append_chat(_L("Assistant"), stub);
        m_messages.push_back({"assistant", stub.utf8_string()});
        trim_message_history();
        MakerWorldImportFlow::run_user_makerworld_request(this, user_utf8, m_apply_mode, makerworld_flow_callbacks());
        return;
    }

    size_t user_turns = 0;
    for (const auto& m : m_messages)
        if (m.role == "user")
            ++user_turns;

    bool attach_context =
        (user_turns == 0) || (m_apply_mode ? (user_turns % 2 == 0) : (user_turns % 4 == 0));

    std::string user_msg = user_text.utf8_string();
    if (!m_apply_mode) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        user_msg = ko ? std::string("질문 (설명만, 슬라이서 변경 금지):\n") + user_msg
                      : std::string("Question (explain only — do not change slicer settings):\n") + user_msg;
    }
    if (attach_context) {
        std::string context = (user_turns == 0)
            ? OllamaActionExecutor::build_compact_context_json()
            : OllamaActionExecutor::build_context_json();
        if (m_apply_mode) {
            try {
                nlohmann::json ctx = nlohmann::json::parse(context);
                const bool     ko  = wxGetApp().current_language_code().StartsWith("ko");
                ctx["pro_tips"]    = OllamaPrintingTips::tips_for_request(user_utf8, ko);
                context            = ctx.dump(2);
            } catch (...) {
            }
        }
        context = OllamaActionExecutor::fit_context_json_to_limit(std::move(context), kMaxContextChars);
        user_msg = std::string("Current slicer context (JSON):\n") + context + "\n\nUser request:\n" + user_msg;
    }
    if (m_apply_mode) {
        try {
            const nlohmann::json digest = OllamaAgentStateService::config_digest(user_utf8);
            user_msg += std::string("\n\nConfig digest (JSON):\n") + digest.dump(2);
        } catch (...) {
        }
        if (OllamaAgentGoalPlanner::is_single_shot_apply(user_utf8)) {
            const bool           ko   = wxGetApp().current_language_code().StartsWith("ko");
            const nlohmann::json hint = OllamaAgentGoalPlanner::build_plan_hint(user_utf8, ko);
            user_msg += std::string("\n\nApply plan hint (JSON):\n") + hint.dump(2);
        }
    }

    if (!m_available_models.empty())
        m_model = resolve_installed_model(m_available_models, m_model);

    if (OllamaAssistRouter::should_use_assist_loop(user_utf8, m_apply_mode)) {
        m_messages.push_back({"user", user_msg});
        trim_message_history();
        start_assist_loop_turn(user_utf8);
        return;
    }
    if (OllamaAssistRouter::should_use_two_hop(user_utf8, m_apply_mode)) {
        m_messages.push_back({"user", user_msg});
        trim_message_history();
        start_two_hop_turn(user_utf8);
        return;
    }

    m_messages.push_back({"user", user_msg});
    trim_message_history();

    if (OllamaDiagnosticPipeline::needs_pipeline(user_utf8, m_apply_mode)) {
        start_diagnostic_turn(user_utf8);
        return;
    }

    set_busy(true);
    begin_thinking_block();
    const bool ko_fast = wxGetApp().current_language_code().StartsWith("ko");
    if (m_apply_mode) {
        append_thinking_line(wxString::FromUTF8(ollama_thinking_goal_intro(user_utf8, ko_fast)));
        if (OllamaAgentGoalPlanner::is_single_shot_apply(user_utf8)) {
            const nlohmann::json plan_hint = OllamaAgentGoalPlanner::build_plan_hint(user_utf8, ko_fast);
            if (plan_hint.contains("suggested_steps") && plan_hint["suggested_steps"].is_array()) {
                for (const auto& step : plan_hint["suggested_steps"]) {
                    if (step.is_string())
                        append_thinking_line(wxString::FromUTF8(step.get<std::string>()));
                }
            }
        }
        append_thinking_line(ko_fast ? wxString::FromUTF8("변경안을 만들고 바로 적용합니다…")
                                     : wxString("Preparing and applying changes…"));
    } else {
        append_thinking_line(ko_fast ? wxString::FromUTF8("답변을 준비하고 있습니다…")
                                     : wxString("Preparing an answer…"));
    }
    const std::string model = normalize_ollama_model_tag(m_model);
    const auto        alive = m_alive;
    const uint64_t    gen   = ++m_request_gen;
    wxWeakRef<wxWindow> weak_panel(this);
    m_client.chat(model, m_messages, [alive, gen, weak_panel](const std::string& text, const std::string& error) {
        wxGetApp().CallAfter([alive, gen, weak_panel, text, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            auto* panel = dynamic_cast<OllamaChatPanel*>(weak_panel.get());
            if (!panel || panel->m_request_gen != gen)
                return;
            panel->on_chat_response(text, error);
        });
    });
}

void OllamaChatPanel::start_assist_loop_turn(const std::string& user_utf8)
{
    set_busy(true);
    begin_thinking_block();
    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    append_thinking_line(wxString::FromUTF8(ollama_thinking_goal_intro(user_utf8, ko)));
    const nlohmann::json plan_hint = OllamaAgentGoalPlanner::build_plan_hint(user_utf8, ko);
    if (plan_hint.contains("suggested_steps") && plan_hint["suggested_steps"].is_array()) {
        for (const auto& step : plan_hint["suggested_steps"]) {
            if (step.is_string())
                append_thinking_line(wxString::FromUTF8(step.get<std::string>()));
        }
    }

    if (!m_assist_controller)
        m_assist_controller = std::make_unique<OllamaAgentController>(m_client, normalize_ollama_model_tag(m_model));
    else
        m_assist_controller->set_model(normalize_ollama_model_tag(m_model));

    const auto alive = m_alive;
    m_assist_controller->run_goal(
        user_utf8, ollama_execution_policy_for_assist_loop(), this,
        OllamaAgentCallbacks{
            [alive, this](const wxString& line) {
                if (!alive->load())
                    return;
                append_thinking_line(line);
            },
            [alive, this](const OllamaAgentRunResult& result) {
                if (!alive->load())
                    return;
                on_assist_loop_finished(result);
            },
        },
        ollama_assist_max_steps());
}

void OllamaChatPanel::on_assist_loop_finished(const OllamaAgentRunResult& result)
{
    if (!m_alive->load() || wxGetApp().is_closing())
        return;

    clear_thinking_block();
    set_busy(false);

    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    wxString   display;
    if (result.completed && !result.step_tool_results.empty()) {
        const std::string summary =
            ollama_user_facing_summary(result.step_tool_results, result.final_message, ko);
        if (!summary.empty())
            display = wxString::FromUTF8(summary);
    }
    if (display.empty() && !result.final_message.empty())
        display = wxString::FromUTF8(result.final_message);
    else if (display.empty() && result.cancelled)
        display = ko ? wxString::FromUTF8("작업이 취소되었습니다.") : _L("Cancelled.");
    else if (display.empty() && result.blocked)
        display = ko ? wxString::FromUTF8("작업을 마치지 못했습니다. 플레이트에서 모델을 선택한 뒤 다시 시도해 주세요.")
                     : _L("Couldn't finish that. Select a model on the plate and try again.");
    else if (display.empty())
        display = _L("OK.");

    m_messages.push_back({"assistant", into_u8(display)});
    trim_message_history();
    append_chat(_L("Assistant"), display);
    set_status_text(completion_status_for_reply(display, m_apply_mode));
}

void OllamaChatPanel::start_two_hop_turn(const std::string& user_utf8)
{
    set_busy(true);
    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    append_thinking_line(ko ? wxString::FromUTF8("관련 설정 찾는 중…") : wxString("Finding relevant settings…"));

    std::vector<OllamaMessage> planner_msgs;
    planner_msgs.push_back({"system", OllamaActionExecutor::build_planner_system_prompt()});
    planner_msgs.push_back({"user", OllamaActionExecutor::build_planner_user_message(user_utf8)});

    const std::string   model = normalize_ollama_model_tag(m_model);
    const auto          alive = m_alive;
    const uint64_t      gen   = ++m_request_gen;
    wxWeakRef<wxWindow> weak_panel(this);
    m_client.chat(
        model, planner_msgs,
        [alive, gen, weak_panel, user_utf8](const std::string& text, const std::string& error) {
            wxGetApp().CallAfter([alive, gen, weak_panel, user_utf8, text, error]() {
                if (!alive->load() || wxGetApp().is_closing())
                    return;
                auto* panel = dynamic_cast<OllamaChatPanel*>(weak_panel.get());
                if (!panel || panel->m_request_gen != gen)
                    return;
                panel->on_two_hop_planner_response(text, user_utf8, error);
            });
        },
        OllamaRequestKind::Planner);
}

void OllamaChatPanel::on_two_hop_planner_response(const std::string& planner_text, const std::string& user_utf8,
                                                  const std::string& error)
{
    if (!error.empty()) {
        clear_thinking_block();
        set_busy(false);
        append_chat(_L("Assistant"), format_chat_error(error));
        set_status_text(format_chat_error(error));
        return;
    }

    std::vector<std::string> keys = OllamaSettingSearch::keys_from_planner_json(planner_text);
    if (keys.empty())
        keys = OllamaSettingSearch::candidate_keys_for_request(user_utf8, 3, 10);

    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    append_thinking_line(ko ? wxString::FromUTF8("변경안 준비 중…") : wxString("Preparing changes…"));

    nlohmann::json wiki_context = nlohmann::json::array();
    if (ollama_wiki_search_enabled() && OllamaRequestRouter::benefits_from_wiki(user_utf8)) {
        OllamaDiagnosis pseudo;
        pseudo.wiki_queries.push_back(user_utf8);
        wiki_context = OllamaDiagnosticPipeline::build_wiki_evidence(pseudo, user_utf8, ko);
    }

    const std::string resolver_user =
        OllamaActionExecutor::build_resolver_user_message(user_utf8, keys, wiki_context);
    launch_single_chat_llm(resolver_user);
}

void OllamaChatPanel::launch_single_chat_llm(std::string final_user_msg)
{
    m_messages.push_back({"user", std::move(final_user_msg)});
    trim_message_history();
    if (!m_available_models.empty())
        m_model = resolve_installed_model(m_available_models, m_model);
    set_busy(true);
    const std::string   model = normalize_ollama_model_tag(m_model);
    const auto          alive = m_alive;
    const uint64_t      gen   = ++m_request_gen;
    wxWeakRef<wxWindow> weak_panel(this);
    m_client.chat(model, m_messages, [alive, gen, weak_panel](const std::string& text, const std::string& error) {
        wxGetApp().CallAfter([alive, gen, weak_panel, text, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            auto* panel = dynamic_cast<OllamaChatPanel*>(weak_panel.get());
            if (!panel || panel->m_request_gen != gen)
                return;
            panel->on_chat_response(text, error);
        });
    });
}

void OllamaChatPanel::start_diagnostic_turn(const std::string& user_utf8)
{
    set_busy(true);
    const uint64_t gen = ++m_request_gen;
    const bool     ko  = wxGetApp().current_language_code().StartsWith("ko");
    set_status_text(ko ? wxString::FromUTF8("원인 파악 중…") : _L("Figuring out the issue…"));
    append_thinking_line(ko ? wxString::FromUTF8("1. 증상 확인 중…") : wxString("1. Reviewing what you reported…"));

    std::vector<OllamaMessage> diag_msgs;
    diag_msgs.push_back({"system", OllamaActionExecutor::build_diagnostic_system_prompt()});
    diag_msgs.push_back({"user", OllamaActionExecutor::build_diagnostic_user_message(user_utf8)});

    const std::string   model = m_model.empty() ? std::string(kOllamaDefaultModel) : normalize_ollama_model_tag(m_model);
    const auto          alive = m_alive;
    wxWeakRef<wxWindow> weak_panel(this);
    m_client.chat(model, diag_msgs,
                  [alive, gen, weak_panel, user_utf8](const std::string& text, const std::string& error) {
                      wxGetApp().CallAfter([alive, gen, weak_panel, user_utf8, text, error]() {
                          if (!alive->load() || wxGetApp().is_closing())
                              return;
                          auto* panel = dynamic_cast<OllamaChatPanel*>(weak_panel.get());
                          if (!panel || panel->m_request_gen != gen)
                              return;
                          panel->on_diagnosis_response(text, user_utf8, error);
                      });
                  },
                  OllamaRequestKind::Planner);
}

void OllamaChatPanel::on_diagnosis_response(const std::string& diagnosis_text, const std::string& user_utf8,
                                            const std::string& error)
{
    if (!error.empty()) {
        clear_thinking_block();
        set_busy(false);
        append_chat(_L("Assistant"), format_chat_error(error));
        set_status_text(format_chat_error(error));
        return;
    }

    const OllamaDiagnosis diagnosis = OllamaDiagnosticPipeline::parse_diagnosis(diagnosis_text);
    const bool            ko        = wxGetApp().current_language_code().StartsWith("ko");
    append_thinking_text(thinking_diagnosis_lines(diagnosis, ko));
    append_thinking_line(ko ? wxString::FromUTF8("2. 도움말 검색 중…") : wxString("2. Looking up guidance…"));
    set_status_text(ko ? wxString::FromUTF8("도움말 검색 중…") : _L("Looking up guidance…"));

    std::vector<std::string> keys = diagnosis.candidate_keys;
    if (keys.empty())
        keys = OllamaSettingSearch::candidate_keys_for_request(user_utf8, 3, 10);

    const auto          alive = m_alive;
    const uint64_t      gen   = m_request_gen;
    wxWeakRef<wxWindow> weak_panel(this);
    std::thread([alive, gen, weak_panel, user_utf8, diagnosis, keys = std::move(keys), ko]() mutable {
        nlohmann::json wiki = nlohmann::json::array();
        if (ollama_wiki_search_enabled())
            wiki = OllamaDiagnosticPipeline::build_wiki_evidence(diagnosis, user_utf8, ko);
        const wxString wiki_lines = thinking_wiki_lines(wiki, ko);
        const nlohmann::json analysis =
            OllamaDiagnosticPipeline::analyze_current_settings(keys, diagnosis, ko);
        const wxString settings_lines = thinking_settings_lines(analysis, ko);

        wxGetApp().CallAfter([alive, gen, weak_panel, user_utf8, diagnosis, keys = std::move(keys), wiki, analysis,
                              wiki_lines, settings_lines, ko]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            auto* panel = dynamic_cast<OllamaChatPanel*>(weak_panel.get());
            if (!panel || panel->m_request_gen != gen)
                return;
            if (!wiki_lines.IsEmpty())
                panel->append_thinking_text(wiki_lines);
            panel->append_thinking_line(ko ? wxString::FromUTF8("3. 현재 설정 확인 중…")
                                           : wxString("3. Checking current settings…"));
            if (!settings_lines.IsEmpty())
                panel->append_thinking_text(settings_lines);
            panel->append_thinking_line(ko ? wxString::FromUTF8("4. 변경안 만들기…")
                                           : wxString("4. Building a fix…"));
            const bool ko_ui = wxGetApp().current_language_code().StartsWith("ko");
            panel->set_status_text(ko_ui ? wxString::FromUTF8("변경안 만들기…") : _L("Building a fix…"));
            panel->launch_proposal_llm(user_utf8, diagnosis, std::move(keys), wiki, analysis, 0);
        });
    }).detach();
}

void OllamaChatPanel::launch_proposal_llm(const std::string& user_utf8, const OllamaDiagnosis& diagnosis,
                                          std::vector<std::string> keys, const nlohmann::json& wiki_context,
                                          const nlohmann::json& settings_analysis, int critic_attempt)
{
    nlohmann::json diagnosis_summary = nlohmann::json::object();
    diagnosis_summary["symptom"]       = diagnosis.symptom;
    diagnosis_summary["diagnosis"]     = diagnosis.diagnosis;
    diagnosis_summary["likely_causes"] = diagnosis.likely_causes;
    diagnosis_summary["message"]     = diagnosis.user_message;
    diagnosis_summary["step"] =
        wxGetApp().current_language_code().StartsWith("ko") ? "문제 진단" : "Problem diagnosis";

    std::vector<OllamaMessage> proposal_msgs;
    proposal_msgs.push_back({"system", OllamaActionExecutor::build_system_prompt(true)});
    proposal_msgs.push_back({"user", OllamaActionExecutor::build_proposal_user_message(
                                        user_utf8, keys, diagnosis_summary, wiki_context, settings_analysis)});

    const std::string   model = m_model.empty() ? std::string(kOllamaDefaultModel) : normalize_ollama_model_tag(m_model);
    const auto          alive = m_alive;
    const uint64_t      gen   = m_request_gen;
    wxWeakRef<wxWindow> weak_panel(this);
    m_client.chat(model, proposal_msgs,
                  [alive, gen, weak_panel, user_utf8, keys, wiki_context, critic_attempt](const std::string& text,
                                                                                            const std::string& error) {
                      wxGetApp().CallAfter([alive, gen, weak_panel, user_utf8, keys, wiki_context, critic_attempt, text,
                                            error]() {
                          if (!alive->load() || wxGetApp().is_closing())
                              return;
                          auto* panel = dynamic_cast<OllamaChatPanel*>(weak_panel.get());
                          if (!panel || panel->m_request_gen != gen)
                              return;
                          panel->on_proposal_llm_response(text, error, user_utf8, keys, wiki_context, critic_attempt);
                      });
                  },
                  OllamaRequestKind::Resolver);
}

void OllamaChatPanel::on_proposal_llm_response(const std::string& assistant_text, const std::string& error,
                                               const std::string& user_utf8, std::vector<std::string> keys,
                                               const nlohmann::json& wiki_context, int critic_attempt)
{
    if (!error.empty()) {
        clear_thinking_block();
        set_busy(false);
        append_chat(_L("Assistant"), format_chat_error(error));
        set_status_text(format_chat_error(error));
        return;
    }

    const bool use_critic =
        ollama_critic_enabled() || OllamaRequestRouter::classify(user_utf8) == OllamaRequestRoute::Deep;
    if (use_critic && critic_attempt < 1) {
        try {
            nlohmann::json root = OllamaActionPipeline::extract_from_assistant_text(assistant_text);
            OllamaResponseNormalizer::normalize(root, user_utf8, /*include_makerworld*/ true);
            const OllamaCriticResult critic =
                OllamaActionCritic::review(root, user_utf8, wiki_context);
            if (critic.verdict == OllamaCriticVerdict::Revise && !critic.suggested_keys.empty()) {
                std::unordered_set<std::string> merged(keys.begin(), keys.end());
                for (const std::string& k : critic.suggested_keys)
                    merged.insert(k);
                keys.assign(merged.begin(), merged.end());
                OllamaDiagnosis      diag = OllamaDiagnosticPipeline::parse_diagnosis("{}");
                const bool           ko   = wxGetApp().current_language_code().StartsWith("ko");
                append_thinking_line(ko ? wxString::FromUTF8("답변 다듬는 중…") : wxString("Refining answer…"));
                const nlohmann::json analysis =
                    OllamaDiagnosticPipeline::analyze_current_settings(keys, diag, ko);
                launch_proposal_llm(user_utf8, diag, std::move(keys), wiki_context, analysis, critic_attempt + 1);
                return;
            }
        } catch (...) {
        }
    }

    on_chat_response(assistant_text, error);
}

void OllamaChatPanel::retry_last_chat_simple()
{
    if (m_busy || m_messages.empty())
        return;

    drop_last_assistant_message(m_messages);

    for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it) {
        if (it->role != "user")
            continue;
        const std::string marker = "\n\nUser request:\n";
        const auto pos = it->content.rfind(marker);
        if (pos != std::string::npos)
            it->content = it->content.substr(pos + marker.size());
        break;
    }

    set_busy(true);
    set_status_text(ui_is_korean() ? wxString::FromUTF8("다시 시도 중…") : _L("Retrying…"));
    append_thinking_line(ui_is_korean() ? wxString::FromUTF8("다시 시도 중…") : _L("Retrying…"));

    const std::string model = m_model.empty() ? std::string(kOllamaDefaultModel) : normalize_ollama_model_tag(m_model);
    const auto alive        = m_alive;
    const uint64_t gen      = m_request_gen;
    wxWeakRef<wxWindow> weak_panel(this);
    m_client.chat(model, m_messages, [alive, gen, weak_panel](const std::string& text, const std::string& error) {
        wxGetApp().CallAfter([alive, gen, weak_panel, text, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            auto* panel = dynamic_cast<OllamaChatPanel*>(weak_panel.get());
            if (!panel || panel->m_request_gen != gen)
                return;
            panel->on_chat_response(text, error);
        });
    });
}

void OllamaChatPanel::on_models_loaded(const std::vector<std::string>& models, const std::string& error)
{
    if (!error.empty()) {
        ++m_model_poll_failures;
        set_status_text(format_chat_error(error));
        if (m_model_poll_failures < kMaxModelPollFailures)
            schedule_model_poll(5000);
        return;
    }

    m_model_poll_failures = 0;
    m_available_models    = models;
    OllamaServerManager::note_serve_reachable();
    m_model = resolve_installed_model(models, m_model);
    if (m_assist_controller)
        m_assist_controller->set_model(normalize_ollama_model_tag(m_model));
    update_model_label_ui();
    save_settings();
    ensure_default_model_ready(models);
    set_status_text(ui_is_korean()
        ? wxString::Format(wxString::FromUTF8("준비됨 — 모델 %u개"), unsigned(models.size()))
        : wxString::Format(_L("Ready — %u model(s)"), unsigned(models.size())));
}

void OllamaChatPanel::ensure_default_model_ready(const std::vector<std::string>& models)
{
    // If user didn't install a model yet, auto-pull the default model.
    bool has_default = false;
    const std::string& want = m_model.empty() ? std::string(kOllamaDefaultModel) : m_model;
    for (const auto& m : models) {
        if (m == want || m.rfind(want + ":", 0) == 0) {
            has_default = true;
            break;
        }
    }
    if (has_default) {
        m_pull_in_progress = false;
        return;
    }
    if (m_pull_in_progress)
        return;

    m_pull_in_progress = true;
    set_status_text(ui_is_korean() ? wxString::FromUTF8("모델 다운로드 중…") : _L("Downloading model…"));
    const wxString cmd = OllamaServerManager::resolve_ollama_command();
    wxExecute(cmd + " pull " + wxString::FromUTF8(want), wxEXEC_ASYNC);
    schedule_model_poll(5000);
}

void OllamaChatPanel::on_chat_response(const std::string& assistant_text, const std::string& error)
{
    if (!m_alive->load() || wxGetApp().is_closing())
        return;

    clear_thinking_block();
    set_busy(false);

    const bool empty_reply_error = !error.empty() &&
        (error.find("Empty Ollama reply") != std::string::npos || error.find("Empty HTTP body") != std::string::npos);

    if (!error.empty()) {
        if (error.find("HTTP 404") != std::string::npos && m_empty_reply_retries < 1) {
            ++m_empty_reply_retries;
            refresh_models();
            retry_last_chat_simple();
            return;
        }
        if (empty_reply_error && m_empty_reply_retries < 2) {
            ++m_empty_reply_retries;
            retry_last_chat_simple();
            return;
        }
        append_chat(_L("Assistant"), format_chat_error(error));
        set_status_text(format_chat_error(error));
        return;
    }

    {
        std::string trimmed = assistant_text;
        boost::trim(trimmed);
        if (trimmed.empty()) {
            if (m_empty_reply_retries < 2) {
                ++m_empty_reply_retries;
                retry_last_chat_simple();
                return;
            }
            const wxString msg = format_chat_error("Empty assistant response");
            append_chat(_L("Assistant"), msg);
            set_status_text(msg);
            return;
        }
    }

    m_empty_reply_retries = 0;

    wxString display;
    try {
        nlohmann::json root;
        if (!m_apply_mode) {
            try {
                root = OllamaActionPipeline::extract_from_assistant_text(assistant_text);
            } catch (...) {
                display = wxString::FromUTF8(assistant_text);
                push_assistant_history_stub(m_messages,
                    "(question mode — assistant reply omitted from history due to parse error)");
                trim_message_history();
                append_chat(_L("Assistant"), display);
                set_status_text(completion_status_for_reply(display, m_apply_mode));
                return;
            }
        } else {
            root = OllamaActionPipeline::extract_from_assistant_text(assistant_text);
        }
        const std::string user_req = last_user_request_text(m_messages);
        if (m_apply_mode)
            BambuSmartPrint::PrintGoalSession::instance().merge_goal(
                BambuSmartPrint::PrintPlanner::parse_goal(user_req));
        OllamaPipelineOptions opt;
        opt.apply_mode          = m_apply_mode;
        opt.include_makerworld  = true;
        opt.question_mode_strip = !m_apply_mode;
        opt.user_request        = user_req;
        const OllamaPipelineResult piped = OllamaActionPipeline::process_actions(root, opt);
        const OllamaActionSanitizeResult& sanitized = piped.sanitized;
        if (!m_apply_mode)
            OllamaActionPipeline::strip_actions_for_question_history(root);

        if (root.contains("message") && root["message"].is_string()) {
            display = wxString::FromUTF8(root["message"].get<std::string>());
        } else {
            display = _L("OK.");
        }
        m_messages.push_back({"assistant", assistant_history_content(assistant_text, m_apply_mode)});
        trim_message_history();

        const auto mw_callbacks         = makerworld_flow_callbacks();
        const bool makerworld_handled = process_makerworld_actions(root, this, m_apply_mode, user_req, mw_callbacks);

        if (makerworld_handled
            && (!root.contains("actions") || !root["actions"].is_array() || root["actions"].empty())) {
            append_chat(_L("Assistant"), display);
            if (!m_busy)
                set_status_text(completion_status_for_reply(display, m_apply_mode));
            return;
        }

        if (m_apply_mode) {
            const OllamaExecutionPolicy policy = ollama_execution_policy_for_assist_mode();
            nlohmann::json              exec_root = root;
            if (sanitized.blocked_count > 0
                && (!exec_root.contains("actions") || !exec_root["actions"].is_array()
                    || exec_root["actions"].empty())) {
                nlohmann::json recovery =
                    OllamaActionPipeline::build_recovery_root(assistant_text, user_req, true);
                if (recovery.contains("actions") && recovery["actions"].is_array()
                    && !recovery["actions"].empty()) {
                    exec_root = std::move(recovery);
                    OllamaActionPipeline::process_actions(exec_root, opt);
                }
            }

            const OllamaWorkflowRun workflow =
                OllamaActionWorkflow::execute_with_policy(exec_root, this, policy);

            const wxString summary = summarize_applied_changes(user_req, workflow.results);

            if (workflow.cancelled) {
                if (!summary.IsEmpty())
                    display = summary;
                display += "\n\n" + cancelled_no_changes_msg();
            } else if (workflow.preview_only) {
                if (!summary.IsEmpty())
                    display = summary;
                display += "\n\n" + preview_only_msg();
            } else if (workflow_had_effective_change(workflow.results)) {
                display = summary;
            } else if (!workflow.results.empty()) {
                display = summary;
            } else if (exec_root.contains("actions") && exec_root["actions"].is_array()
                       && !exec_root["actions"].empty()) {
                display = nothing_applied_msg();
            } else if (message_looks_like_manual_instruction(into_u8(display))) {
                display = manual_instruction_msg();
            } else if (sanitized.blocked_count > 0) {
                display = change_not_applied_msg();
            }
        } else if (root.contains("actions") && root["actions"].is_array() && !root["actions"].empty()) {
            display += "\n\n" + question_mode_actions_skipped_msg();
        }
    } catch (const std::exception& e) {
        const std::string user_req = last_user_request_text(m_messages);
        if (m_apply_mode && MakerWorldSearchService::is_pure_makerworld_request(user_req)) {
            const bool is_import = MakerWorldSearchService::user_wants_makerworld_import(user_req);
            append_chat(_L("Assistant"), is_import ? _L("Importing from MakerWorld…") : _L("Searching MakerWorld…"));
            MakerWorldImportFlow::run_user_makerworld_request(this, user_req, m_apply_mode, makerworld_flow_callbacks());
            return;
        }
        display = format_assistant_failure(e.what(), assistant_text, m_apply_mode);
        push_assistant_history_stub(m_messages, parse_failure_history_stub(e.what()));
    }

    trim_message_history();
    append_chat(_L("Assistant"), display);
    set_status_text(completion_status_for_reply(display, m_apply_mode));
}

}} // namespace

