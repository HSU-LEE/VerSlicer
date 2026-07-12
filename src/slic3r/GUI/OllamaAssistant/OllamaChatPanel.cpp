#include "OllamaChatPanel.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaActionPipeline.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaModelPick.hpp"
#include "OllamaServerManager.hpp"
#include "OllamaActionWorkflow.hpp"
#include "OllamaAgentGoalPlanner.hpp"
#include "OllamaAgentStateService.hpp"
#include "OllamaConfig.hpp"
#include "OllamaExecutionPolicy.hpp"
#include "OllamaProcessingNotice.hpp"
#include "OllamaToolResult.hpp"
#include "OllamaPrintingTips.hpp"
#include "OllamaTelemetry.hpp"
#include "OllamaUserFlow.hpp"
#include "OllamaVoiceTranscript.hpp"
#include "OllamaSendRouter.hpp"
#include "../MakerWorld/MakerWorldImportFlow.hpp"
#include "../MakerWorld/MakerWorldIntent.hpp"
#include "../MakerWorld/MakerWorldTypes.hpp"
#include "../MakerWorld/MakerWorldUrl.hpp"

#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../BambuSmartPrint/PrintPlannerGui.hpp"
#include "../AIPipeline/PrintJob.hpp"
#include "../AIPipeline/PrintJobOrchestrator.hpp"
#include "../AIPipeline/PrintJobState.hpp"
#include "../AIPipeline/PrintJobUiAdapter.hpp"
#include "AiLocale.hpp"
#include "OllamaChatMessageList.hpp"
#include "OllamaConfigProposalBuilder.hpp"
#include "libslic3r/BambuSmartPrint/PrintGoalParser.hpp"
#include "libslic3r/BambuSmartPrint/PrintGoalSession.hpp"
#include "libslic3r/BambuSmartPrint/PrintIntentSession.hpp"
#include "libslic3r/BambuSmartPrint/PrintPlanner.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/ComboBox.hpp"
#include "../Widgets/ProgressBar.hpp"
#include "../Widgets/TextInput.hpp"

#include <wx/control.h>
#include <wx/dcclient.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/filefn.h>
#include <wx/settings.h>
#include <wx/weakref.h>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

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

static wxString format_chat_error(const std::string& raw)
{
    const bool ko = AiLocale::korean();
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
                + (AiLocale::korean()
                       ? wxString::FromUTF8("설명은 위와 같습니다. 자동 적용은 되지 않았어요 — '브림 켜 줘'처럼 다시 요청해 보세요.")
                       : _L("See above for the explanation. To apply automatically, try a direct request like “turn on brim”."));
        } else if (apply_mode) {
            display += "\n\n"
                + (AiLocale::korean()
                       ? wxString::FromUTF8("위 내용은 참고용입니다. 자동 적용이 되지 않았어요 — 플레이트에서 모델을 선택한 뒤 다시 시도해 주세요.")
                       : _L("See above for context. It wasn't applied automatically — select a model on the plate and try again."));
        } else {
            display += "\n\n"
                + (AiLocale::korean()
                       ? wxString::FromUTF8("답변 중 일부를 처리하지 못했습니다. 질문을 다시 보내 보세요.")
                       : _L("Part of the reply couldn't be processed. Try sending your question again."));
        }
        return display;
    }

    if (parse_error)
        return AiLocale::korean()
            ? wxString::FromUTF8("응답을 이해하지 못했습니다. '브림 켜 줘'처럼 짧게 다시 보내 보세요.")
            : _L("Couldn't understand the reply. Try a short request like “turn on brim”.");

    const wxString mapped = format_chat_error(what);
    if (mapped != wxString::FromUTF8(what))
        return mapped;

    return AiLocale::korean() ? wxString::FromUTF8("문제가 발생했습니다. 잠시 후 다시 시도해 주세요.")
                          : _L("Something went wrong. Try again in a moment.");
}

static wxString change_not_applied_msg()
{
    return AiLocale::korean()
        ? wxString::FromUTF8("아직 적용되지 않았습니다. 플레이트에서 모델을 선택하거나, '브림 켜 줘'처럼 구체적으로 말씀해 주세요.")
        : _L("That wasn't applied yet. Select a model on the plate, or try a clearer request like “turn on brim”.");
}

static wxString cancelled_no_changes_msg()
{
    return AiLocale::korean() ? wxString::FromUTF8("취소되었습니다 — 변경 사항은 없습니다.")
                          : _L("Cancelled — nothing was changed.");
}

static wxString preview_only_msg()
{
    return AiLocale::korean()
        ? wxString::FromUTF8("미리보기만 표시되었습니다. 비교 창을 닫은 뒤 적용 여부를 확인해 주세요.")
        : _L("Preview only — close the compare dialog to finish or cancel.");
}

static wxString nothing_applied_msg()
{
    return AiLocale::korean()
        ? wxString::FromUTF8("적용된 변경이 없습니다. 플레이트에서 모델을 선택한 뒤 다시 시도해 주세요.")
        : _L("Nothing was applied. Select a model on the plate and try again.");
}

static wxString manual_instruction_msg()
{
    return AiLocale::korean()
        ? wxString::FromUTF8("자동으로 적용하지 못했습니다. 플레이트에서 모델을 선택하거나, 적용 모드에서 더 구체적으로 요청해 주세요.")
        : _L("Couldn't apply that automatically. Select a model on the plate, or try a more specific request in Apply mode.");
}

static wxString question_mode_actions_skipped_msg()
{
    return AiLocale::korean() ? wxString::FromUTF8("(질문 모드 — 제안된 변경은 적용하지 않았습니다.)")
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
    const bool ko = AiLocale::korean();
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
        BOOST_LOG_TRIVIAL(warning) << "Ollama chat: failed to strip actions from question history";
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

static wxString completion_status_for_reply(const wxString& reply, bool apply_mode)
{
    if (!apply_mode)
        return AiLocale::korean() ? wxString::FromUTF8("답변 완료") : _L("Answer ready");

    if (reply.IsEmpty())
        return AiLocale::korean() ? wxString::FromUTF8("완료") : _L("Done");

    if (reply.Contains(_L("No models found")) || reply.Contains(wxString::FromUTF8("모델을 찾지 못")))
        return AiLocale::korean() ? wxString::FromUTF8("MakerWorld 검색 완료 — 결과 없음")
                              : _L("MakerWorld: no models found");

    if (reply.Contains(_L("Search cancelled")) || reply.Contains(_L("Import cancelled")))
        return AiLocale::korean() ? wxString::FromUTF8("취소됨") : _L("Cancelled");

    if (reply.Contains(_L("Model loaded")) || reply.Contains(wxString::FromUTF8("불러왔")))
        return AiLocale::korean() ? wxString::FromUTF8("모델 불러오기 완료") : _L("Model loaded");

    if (reply.Contains(_L("Import failed")) || reply.Contains(wxString::FromUTF8("가져오기 실패")))
        return AiLocale::korean() ? wxString::FromUTF8("가져오기 실패") : _L("Import failed");

    if (reply.Contains(_L("Found ")) && reply.Contains(_L("models on MakerWorld")))
        return AiLocale::korean() ? wxString::FromUTF8("MakerWorld 검색 완료") : _L("MakerWorld search done");

    if (reply.Contains(_L("Cancelled")) || reply.Contains(wxString::FromUTF8("취소")))
        return AiLocale::korean() ? wxString::FromUTF8("취소됨") : _L("Cancelled");

    if (reply.Contains(_L("Nothing was applied")) || reply.Contains(_L("wasn't applied"))
        || reply.Contains(wxString::FromUTF8("적용되지 않")) || reply.Contains(wxString::FromUTF8("아직 적용")))
        return AiLocale::korean() ? wxString::FromUTF8("적용된 변경 없음") : _L("Nothing applied");

    if (reply.Contains(wxString::FromUTF8("분할")) || reply.Contains(_L("Split"))
        || reply.Contains(wxString::FromUTF8("구멍")) || reply.Contains(_L("Drilled a hole"))
        || reply.Contains(wxString::FromUTF8("형상")) || reply.Contains(_L("Modified model"))
        || reply.Contains(wxString::FromUTF8("메쉬")) || reply.Contains(_L("Repaired the mesh"))
        || reply.Contains(wxString::FromUTF8("회전")) || reply.Contains(_L("Rotated"))
        || reply.Contains(wxString::FromUTF8("크기를 조정")) || reply.Contains(_L("Resized the model")))
        return AiLocale::korean() ? wxString::FromUTF8("모델 수정 완료") : _L("Model edit done");

    if (reply.Contains(wxString::FromUTF8("브림")) || reply.Contains(wxString::FromUTF8("서포트"))
        || reply.Contains(wxString::FromUTF8("속도")) || reply.Contains(wxString::FromUTF8("켰"))
        || reply.Contains(wxString::FromUTF8("업데이트")) || reply.Contains(_L("Enabled brim"))
        || reply.Contains(_L("Print settings updated")))
        return AiLocale::korean() ? wxString::FromUTF8("설정 반영 완료") : _L("Settings updated");

    if (reply.Contains(wxString::FromUTF8("크기")) || reply.Contains(_L("Resized")))
        return AiLocale::korean() ? wxString::FromUTF8("모델 수정 완료") : _L("Model edit done");

    if (reply.Contains(_L("couldn't understand")) || reply.Contains(wxString::FromUTF8("이해하지 못했")))
        return AiLocale::korean() ? wxString::FromUTF8("다시 입력해 주세요") : _L("Try again");

    if (reply.Contains(wxString::FromUTF8("플레이트")) || reply.Contains(_L("build plate")))
        return AiLocale::korean() ? wxString::FromUTF8("플레이트 추가 완료") : _L("Plate added");

    // LLM-only "I'll print/slice…" without orchestrator progress evidence — do not
    // read as a successful print job.
    if (reply.Contains(wxString::FromUTF8("출력을 시작")) || reply.Contains(wxString::FromUTF8("슬라이스를 생성"))
        || reply.Contains(wxString::FromUTF8("묶어서 실행")))
        return AiLocale::korean() ? wxString::FromUTF8("확인 필요 — 플레이트를 확인하세요") : _L("Verify on the plate");

    return AiLocale::korean() ? wxString::FromUTF8("완료") : _L("Done");
}

// Strip a trailing incomplete UTF-8 sequence so a partial stream buffer can be
// converted without wxString::FromUTF8 rejecting the whole string.
static std::string utf8_complete_prefix(std::string s)
{
    size_t i = s.size();
    size_t trailing = 0;
    while (i > 0 && trailing < 4) {
        const unsigned char c = static_cast<unsigned char>(s[i - 1]);
        if ((c & 0x80) == 0)
            return s; // ends on ASCII — complete
        if ((c & 0xC0) == 0xC0) {
            // Lead byte: check whether its sequence is complete.
            size_t need = 2;
            if ((c & 0xF0) == 0xE0)
                need = 3;
            else if ((c & 0xF8) == 0xF0)
                need = 4;
            if (trailing + 1 < need)
                s.resize(i - 1); // incomplete sequence — drop it
            return s;
        }
        // Continuation byte — keep scanning backwards.
        ++trailing;
        --i;
    }
    return s;
}

// Best-effort live preview of a streaming reply: JSON replies stream the
// partial "message" string value; plain-text replies stream verbatim.
static wxString streaming_preview_text(const std::string& buf)
{
    std::string t = buf;
    boost::trim_left(t);
    const bool looks_json = !t.empty() && (t[0] == '{' || t.rfind("```", 0) == 0);
    if (!looks_json)
        return wxString::FromUTF8(utf8_complete_prefix(buf));

    const size_t key = t.find("\"message\"");
    if (key == std::string::npos)
        return {};
    const size_t colon = t.find(':', key + 9);
    if (colon == std::string::npos)
        return {};
    const size_t quote = t.find('"', colon + 1);
    if (quote == std::string::npos)
        return {};
    std::string out;
    bool        esc = false;
    for (size_t i = quote + 1; i < t.size(); ++i) {
        const char c = t[i];
        if (esc) {
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': break;
            default: out += c; break;
            }
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"')
            break;
        out += c;
    }
    return wxString::FromUTF8(utf8_complete_prefix(out));
}

// Most recently created panel; the agent loop routes makerworld_find_and_print
// through it so the orchestrator always runs with real chat/busy callbacks.
OllamaChatPanel* g_active_chat_panel = nullptr;

} // namespace

OllamaChatPanel* OllamaChatPanel::active_panel()
{
    return g_active_chat_panel;
}

static bool process_makerworld_actions(nlohmann::json& root, wxWindow* parent, bool apply_mode,
                                       const std::string& user_req, MakerWorldFlowUiCallbacks callbacks)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;

    bool handled = false;
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& action : root["actions"]) {
        // Route MakerWorld actions through the shared executor helper so the single-shot
        // chat path and the agent loop stay in sync (see MakerWorldImportFlow::run_agent_action).
        if (MakerWorldImportFlow::run_agent_action(action, parent, apply_mode, user_req, callbacks))
            handled = true;
        else
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
            AiLocale::korean() ? wxString::FromUTF8("AI 도우미")
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
    // Toolbar — two rows so the narrow right-docked panel doesn't clip the
    // Korean "초기화" label or squeeze the model/mode combos into double-border
    // looking boxes.
    const int pad       = SlicePilotUi::content_side_margin_dip(m_body, false);
    const int gap       = FromDIP(8);
    const int row_h     = FromDIP(28);
    const int compose_h = FromDIP(32);

    auto* toolbar = new wxBoxSizer(wxVERTICAL);

    auto* model_row = new wxBoxSizer(wxHORIZONTAL);
    auto* model_key = new wxStaticText(m_body, wxID_ANY, _L("Model"));
    model_key->SetForegroundColour(SlicePilotUi::Theme::text_muted());
    model_key->SetFont(Label::Body_13);
    m_model_combo = new ComboBox(m_body, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                 wxDefaultSize, 0, nullptr, wxCB_READONLY);
    SlicePilotUi::style_orca_combobox(m_model_combo);
    m_model_combo->SetFont(Label::Body_13);
    m_model_combo->GetDropDown().SetFont(Label::Body_13);
    m_model_combo->SetMinSize(wxSize(FromDIP(120), row_h));
    m_model_combo->SetMaxSize(wxSize(-1, row_h));
    m_model_combo->Append(wxString::FromUTF8(kOllamaDefaultModel));
    SlicePilotUi::sync_combobox_selection(m_model_combo, 0);
    model_row->Add(model_key, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    model_row->Add(m_model_combo, 1, wxALIGN_CENTER_VERTICAL);

    auto* mode_row = new wxBoxSizer(wxHORIZONTAL);
    m_mode_label = new wxStaticText(m_body, wxID_ANY, _L("Mode"));
    m_mode_label->SetForegroundColour(SlicePilotUi::Theme::text_muted());
    m_mode_label->SetFont(Label::Body_13);
    m_mode_combo = new ComboBox(m_body, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxDefaultSize, 0, nullptr, wxCB_READONLY);
    SlicePilotUi::style_orca_combobox(m_mode_combo);
    m_mode_combo->SetFont(Label::Body_13);
    m_mode_combo->GetDropDown().SetFont(Label::Body_13);
    m_mode_combo->SetMinSize(wxSize(FromDIP(88), row_h));
    m_mode_combo->SetMaxSize(wxSize(FromDIP(120), row_h));
    m_mode_combo->Append(AiLocale::text(_L("Question"), "질문"));
    m_mode_combo->Append(AiLocale::text(_L("Apply"), "적용"));
    SlicePilotUi::sync_combobox_selection(m_mode_combo, 1);

    m_reset_btn = new Button(m_body, AiLocale::text(_L("Reset"), "초기화"));
    SlicePilotUi::style_secondary_button(m_reset_btn);
    m_reset_btn->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
    m_reset_btn->SetFont(Label::Body_13);
    m_reset_btn->SetPaddingSize(wxSize(FromDIP(10), FromDIP(4)));
    {
        wxClientDC dc(m_reset_btn);
        dc.SetFont(m_reset_btn->GetFont());
        wxSize te;
        dc.GetTextExtent(m_reset_btn->GetLabel(), &te.x, &te.y);
        // Width must fit Korean "초기화"; do not clamp MaxSize or Compact padding clips it.
        const int reset_w = std::max(te.x + FromDIP(20), FromDIP(56));
        m_reset_btn->SetMinSize(wxSize(reset_w, row_h));
    }

    mode_row->Add(m_mode_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    mode_row->Add(m_mode_combo, 0, wxALIGN_CENTER_VERTICAL);
    mode_row->AddStretchSpacer(1);
    mode_row->Add(m_reset_btn, 0, wxALIGN_CENTER_VERTICAL);

    toolbar->Add(model_row, 0, wxEXPAND);
    toolbar->Add(mode_row, 0, wxEXPAND | wxTOP, FromDIP(6));
    body_sizer->Add(toolbar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    auto* toolbar_rule = new wxPanel(m_body, wxID_ANY, wxDefaultPosition, wxSize(-1, std::max(1, FromDIP(1))));
    toolbar_rule->SetBackgroundColour(SlicePilotUi::Theme::border());
    body_sizer->Add(toolbar_rule, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

    // Chat transcript: bubble list (user right, assistant/system left)
    m_history_list = new OllamaChatMessageList(m_body);
    body_sizer->Add(m_history_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

    // Pipeline progress strip (hidden unless an orchestrator job is active):
    // step label + progress gauge + stop button.
    m_pipeline_panel = new wxPanel(m_body, wxID_ANY);
    m_pipeline_panel->SetBackgroundColour(SlicePilotUi::Theme::surface_alt());
    {
        auto* strip = new wxBoxSizer(wxHORIZONTAL);
        m_pipeline_step_label = new wxStaticText(m_pipeline_panel, wxID_ANY, wxEmptyString,
                                                 wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        m_pipeline_step_label->SetForegroundColour(SlicePilotUi::Theme::text());
        m_pipeline_step_label->SetFont(Label::Body_13);
        m_pipeline_step_label->SetBackgroundColour(SlicePilotUi::Theme::surface_alt());
        // Keep the step label from stealing width from the gauge on narrow panels.
        m_pipeline_step_label->SetMinSize(wxSize(FromDIP(72), -1));

        // ProgressBar clamps its height to a 14px minimum, so pass a matching
        // height and set the radius to half of it for a clean pill shape (a smaller
        // radius on the clamped height renders as a boxy outline). The widget's own
        // background defaults to white, which bleeds through the rounded corners on
        // the dark strip — repaint it with the strip colour so corners blend.
        const int gauge_h = std::max(FromDIP(14), 14);
        m_pipeline_gauge  = new ProgressBar(m_pipeline_panel, wxID_ANY, 100, wxDefaultPosition,
                                            wxSize(FromDIP(80), gauge_h));
        m_pipeline_gauge->SetBackgroundColour(SlicePilotUi::Theme::surface_alt());
        m_pipeline_gauge->SetMinSize(wxSize(FromDIP(64), gauge_h));
        m_pipeline_gauge->SetProgressBackgroundColour(SlicePilotUi::Theme::border());
        m_pipeline_gauge->SetProgressForedColour(SlicePilotUi::Theme::primary());
        m_pipeline_gauge->SetRadius(gauge_h / 2.0);

        m_pipeline_stop_btn = new Button(m_pipeline_panel, AiLocale::text(_L("Stop"), "중지"));
        SlicePilotUi::style_secondary_button(m_pipeline_stop_btn);
        m_pipeline_stop_btn->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
        m_pipeline_stop_btn->SetFont(Label::Body_13);
        m_pipeline_stop_btn->SetPaddingSize(wxSize(FromDIP(10), FromDIP(4)));
        {
            wxClientDC dc(m_pipeline_stop_btn);
            dc.SetFont(m_pipeline_stop_btn->GetFont());
            wxSize te;
            dc.GetTextExtent(m_pipeline_stop_btn->GetLabel(), &te.x, &te.y);
            m_pipeline_stop_btn->SetMinSize(wxSize(std::max(te.x + FromDIP(20), FromDIP(48)), row_h));
        }

        strip->Add(m_pipeline_step_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
        strip->Add(m_pipeline_gauge, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));
        strip->Add(m_pipeline_stop_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

        auto* strip_col = new wxBoxSizer(wxVERTICAL);
        strip_col->Add(strip, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));
        m_pipeline_panel->SetSizer(strip_col);
    }
    m_pipeline_panel->Hide();
    body_sizer->Add(m_pipeline_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

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
    // Re-elide the placeholder whenever the compose row is resized.
    m_input_field->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        evt.Skip();
        CallAfter([this]() { update_input_hint(); });
    });

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
                                AiLocale::korean() ? wxString::FromUTF8("준비됨") : _L("Ready"),
                                wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_status->SetForegroundColour(SlicePilotUi::Theme::text_muted());
    m_status->SetFont(Label::Body_13);
    status_row->Add(m_status, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    m_status_host->SetSizer(status_row);

    footer_block->Add(m_status_host, 0, wxEXPAND | wxTOP, FromDIP(6));
    body_sizer->Add(footer_block, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);

    m_body->SetSizer(body_sizer);
    topsizer->Add(m_body, 1, wxEXPAND);

    SetSizer(topsizer);

    m_send_btn->Bind(wxEVT_BUTTON, &OllamaChatPanel::on_send, this);
    m_input_ctrl->Bind(wxEVT_TEXT_ENTER, &OllamaChatPanel::on_send, this);
    if (m_collapse_btn)
        m_collapse_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { set_collapsed(!m_collapsed); });

    m_mode_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
        m_apply_mode = e.GetSelection() != 0;
        save_settings();
        refresh_mode_ui();
        if (!m_messages.empty())
            m_messages.front() = {"system", OllamaActionExecutor::build_system_prompt(m_apply_mode)};
        update_system_welcome_in_chat();
        e.Skip();
    });
    m_model_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
        const int sel = e.GetSelection();
        if (sel >= 0 && sel < static_cast<int>(m_model_combo->GetCount())) {
            m_model = normalize_ollama_model_tag(m_model_combo->GetString(static_cast<unsigned>(sel)).utf8_string());
            if (m_assist_controller)
                m_assist_controller->set_model(m_model);
            save_settings();
        }
        e.Skip();
    });
    m_reset_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { reset_conversation(); });
    m_pipeline_stop_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_orchestrator && m_orchestrator->is_active())
            m_orchestrator->cancel();
        hide_pipeline_progress();
    });

    load_settings();
    reset_conversation();
    set_status_text(AiLocale::korean() ? wxString::FromUTF8("준비됨") : _L("Ready"));

    m_poll_timer = new wxTimer(this);
    m_poll_timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (!m_alive->load() || wxGetApp().is_closing())
            return;
        refresh_models();
    });

    g_active_chat_panel = this;

    ensure_ollama_running();
}

OllamaChatPanel::~OllamaChatPanel()
{
    if (g_active_chat_panel == this)
        g_active_chat_panel = nullptr;
    m_alive->store(false);
    ++m_request_gen;
    OllamaClient::cancel_active_requests(OllamaCancelDomain::Chat);
    if (m_assist_controller) {
        m_assist_controller->cancel();
        m_assist_controller.reset();
    }
    if (m_orchestrator) {
        // Cancel any in-flight print job so pending searches stop and the shared
        // import/slice registries are released before the panel goes away.
        m_orchestrator->cancel();
        m_orchestrator.reset();
    }
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

wxString OllamaChatPanel::thinking_role_label() const
{
    return AiLocale::korean() ? wxString::FromUTF8("처리 중") : _L("Working");
}

void OllamaChatPanel::begin_thinking_block()
{
    if (!m_history_list || m_history_list->has_pending())
        return;
    m_history_list->begin_pending(thinking_role_label());
}

void OllamaChatPanel::append_thinking_line(const wxString& line)
{
    if (line.IsEmpty() || !m_history_list)
        return;
    if (!m_history_list->has_pending())
        m_history_list->begin_pending(thinking_role_label());
    m_history_list->append_pending_line(line);
}

void OllamaChatPanel::append_thinking_text(const wxString& text)
{
    if (text.IsEmpty())
        return;
    wxString trimmed = text;
    trimmed.Trim();
    append_thinking_line(trimmed);
}

void OllamaChatPanel::clear_thinking_block()
{
    if (m_history_list)
        m_history_list->clear_pending();
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

void OllamaChatPanel::focus_input()
{
    if (!m_input_ctrl)
        return;
    m_input_ctrl->SetFocus();
    m_input_ctrl->SetInsertionPointEnd();
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
    if (!m_model_combo)
        return;
    // Rebuild the selector from the installed models; fall back to the single
    // configured model while the list is still loading.
    m_model_combo->Clear();
    int sel = 0;
    if (m_available_models.empty()) {
        m_model_combo->Append(wxString::FromUTF8(m_model));
    } else {
        for (size_t i = 0; i < m_available_models.size(); ++i) {
            m_model_combo->Append(wxString::FromUTF8(m_available_models[i]));
            if (m_available_models[i] == m_model)
                sel = static_cast<int>(i);
        }
    }
    SlicePilotUi::sync_combobox_selection(m_model_combo, sel);
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
    if (m_mode_combo)
        SlicePilotUi::sync_combobox_selection(m_mode_combo, m_apply_mode ? 1 : 0);
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
        return AiLocale::korean()
            ? wxString::FromUTF8("적용 모드입니다. 회전·크기 조정, 구멍 뚫기, 들뜸·수축 줄이기처럼 원하는 결과를 말씀해 주세요.")
            : _L("Apply mode: say what you want — rotate, resize, drill a hole, fix warping, and more.");
    }
    return AiLocale::korean()
        ? wxString::FromUTF8("질문 모드입니다. 인쇄·설정에 대해 물어보세요. 프로그램 설정은 바꾸지 않습니다.")
        : _L("Question mode: ask anything about printing. Nothing will be changed — you'll get a plain-language explanation.");
}

void OllamaChatPanel::refresh_mode_ui()
{
    if (m_mode_combo) {
        const int sel = m_apply_mode ? 1 : 0;
        if (m_mode_combo->GetSelection() != sel)
            SlicePilotUi::sync_combobox_selection(m_mode_combo, sel);
    }
    if (m_input_ctrl) {
        if (m_apply_mode) {
            m_input_hint_full = AiLocale::korean()
                ? wxString::FromUTF8("예: 45도 돌려 줘 · 50%로 줄여 줘 · 중간에 구멍 뚫어 줘 · 코너 들뜸 줄여 줘")
                : _L("e.g. rotate 45°, scale to 50%, drill a hole in the center, reduce corner warping");
        } else {
            m_input_hint_full = AiLocale::korean()
                ? wxString::FromUTF8("예: 브림이 뭐예요? · 서포트는 언제 필요해? · MakerWorld에서 드래곤 찾아줘")
                : _L("e.g. What is a brim? When do I need supports? Search MakerWorld for a dragon");
        }
        update_input_hint();
    }
}

void OllamaChatPanel::update_input_hint()
{
    if (!m_input_ctrl || m_input_hint_full.IsEmpty())
        return;
    const int avail = m_input_ctrl->GetClientSize().GetWidth() - FromDIP(8);
    wxString  hint  = m_input_hint_full;
    if (avail > FromDIP(40)) {
        wxClientDC dc(m_input_ctrl);
        dc.SetFont(m_input_ctrl->GetFont());
        hint = wxControl::Ellipsize(m_input_hint_full, dc, wxELLIPSIZE_END, avail);
    }
    m_input_ctrl->SetHint(hint);
    // The full example list stays reachable via the tooltip when elided.
    m_input_ctrl->SetToolTip(hint == m_input_hint_full ? wxString{} : m_input_hint_full);
}

void OllamaChatPanel::set_status_text(const wxString& text)
{
    if (!m_status)
        return;
    // Single-line status; long text ellipsizes (wxST_ELLIPSIZE_END) with the
    // full message available as a tooltip.
    wxString label = text;
    if (label.Find('\n') != wxNOT_FOUND)
        label = label.BeforeFirst('\n').Trim();
    if (m_status->GetLabel() == label)
        return;
    m_status->SetLabel(label);
    m_status->SetToolTip(text);
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
    if (!m_history_list)
        return;
    m_history_list->set_first_system_message(system_welcome_message());
}

void OllamaChatPanel::reset_conversation()
{
    ++m_request_gen;
    if (m_assist_controller && m_assist_controller->is_running())
        m_assist_controller->cancel();
    set_busy(false);
    hide_pipeline_progress();
    m_messages.clear();
    m_messages.push_back({"system", OllamaActionExecutor::build_system_prompt(m_apply_mode)});
    m_stream_buf.clear();
    if (m_history_list)
        m_history_list->clear();
    append_chat(_L("System"), system_welcome_message());
    set_status_text(AiLocale::korean() ? wxString::FromUTF8("준비됨") : _L("Ready"));
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
                panel->set_status_text(AiLocale::korean() ? wxString::FromUTF8("준비됨") : _L("Ready"));
                return;
            }

            if (OllamaServerManager::should_spawn_serve()) {
                panel->set_status_text(AiLocale::korean() ? wxString::FromUTF8("AI 시작 중…") : _L("Starting AI…"));
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
        set_status_text(AiLocale::korean() ? wxString::FromUTF8("모델 불러오는 중…") : _L("Loading models…"));
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

void OllamaChatPanel::append_chat_message(ChatMessageRole role, const wxString& text, ChatMessageKind kind)
{
    if (!m_history_list)
        return;
    m_history_list->append_message(role, text, kind);
}

void OllamaChatPanel::append_chat(const wxString& role, const wxString& text)
{
    ChatMessageRole mapped = ChatMessageRole::Assistant;
    if (role == _L("You"))
        mapped = ChatMessageRole::User;
    else if (role == _L("System"))
        mapped = ChatMessageRole::System;
    append_chat_message(mapped, text, ChatMessageKind::Normal);
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
            if (auto* panel = weak.get()) {
                const wxString last = panel->m_history_list ? panel->m_history_list->last_assistant_text()
                                                            : wxString{};
                panel->set_status_text(completion_status_for_reply(last, panel->m_apply_mode));
            }
        });
    };
    return cb;
}

AIPipeline::PrintJobUiCallbacks OllamaChatPanel::orchestrator_ui_callbacks()
{
    AIPipeline::PrintJobUiCallbacks ui;
    wxWeakRef<OllamaChatPanel> weak(this);
    // Orchestrator progress lines render as muted "progress" bubbles instead of
    // full-width assistant messages; they still enter the LLM history so the
    // model keeps context about the pipeline.
    ui.append_chat = [weak](const wxString& msg) {
        if (auto* panel = weak.get()) {
            panel->append_chat_message(ChatMessageRole::Assistant, msg, ChatMessageKind::Progress);
            panel->m_messages.push_back({"assistant", msg.utf8_string()});
            panel->trim_message_history();
        }
    };
    // Clarifying questions need a reply — render them as emphasized bubbles.
    ui.append_question = [weak](const wxString& msg) {
        if (auto* panel = weak.get()) {
            panel->append_chat_message(ChatMessageRole::Assistant, msg, ChatMessageKind::Question);
            panel->m_messages.push_back({"assistant", msg.utf8_string()});
            panel->trim_message_history();
        }
    };
    // Terminal failures get the warning-tinted error bubble.
    ui.append_error = [weak](const wxString& msg) {
        if (auto* panel = weak.get()) {
            panel->append_chat_message(ChatMessageRole::Assistant, msg, ChatMessageKind::Error);
            panel->m_messages.push_back({"assistant", msg.utf8_string()});
            panel->trim_message_history();
        }
    };
    ui.set_busy = [weak](bool busy, const wxString& status) {
        if (auto* panel = weak.get()) {
            panel->set_busy(busy);
            if (!status.IsEmpty())
                panel->set_status_text(status);
        }
    };
    ui.on_step = [weak](AIPipeline::PrintJobState state, const wxString& detail) {
        if (auto* panel = weak.get())
            panel->on_pipeline_step(state, detail);
    };
    ui.on_finished = [weak]() {
        wxGetApp().CallAfter([weak]() {
            if (auto* panel = weak.get()) {
                // Belt-and-braces: the job is over, so no indicator may survive
                // even if a set_busy(false) was missed on some path.
                panel->clear_thinking_block();
                panel->hide_pipeline_progress();
                const wxString last = panel->m_history_list ? panel->m_history_list->last_assistant_text()
                                                            : wxString{};
                panel->set_status_text(completion_status_for_reply(last, panel->m_apply_mode));
            }
        });
    };
    return ui;
}

void OllamaChatPanel::on_pipeline_step(AIPipeline::PrintJobState state, const wxString& detail)
{
    using AIPipeline::PrintJobState;

    struct StepInfo { int index; const char* en; const char* ko; };
    // 6 visible pipeline steps: search -> import -> analyze -> configure -> slice -> send.
    StepInfo info{0, "", ""};
    switch (state) {
    case PrintJobState::Searching:
    case PrintJobState::CandidateSelect:  info = {1, "Search",    "검색"};       break;
    case PrintJobState::Importing:        info = {2, "Import",    "가져오기"};   break;
    case PrintJobState::MeshHealth:
    case PrintJobState::Repairing:
    case PrintJobState::GeometryAnalysis: info = {3, "Analyze",   "분석"};       break;
    case PrintJobState::AutoConfig:       info = {4, "Configure", "설정"};       break;
    case PrintJobState::Slicing:
    case PrintJobState::Estimating:       info = {5, "Slice",     "슬라이스"};   break;
    case PrintJobState::ReadyToPrint:
    case PrintJobState::Sending:          info = {6, "Send",      "전송"};       break;
    default:
        // Idle / intent / clarify / terminal states: no strip.
        hide_pipeline_progress();
        return;
    }

    if (!m_pipeline_panel || !m_pipeline_step_label || !m_pipeline_gauge)
        return;

    constexpr int total = 6;
    wxString label = wxString::Format("%d/%d  %s", info.index, total, AiLocale::text(info.en, info.ko));
    if (!detail.IsEmpty())
        label += wxT(" — ") + detail;
    m_pipeline_step_label->SetLabel(label);
    m_pipeline_step_label->SetToolTip(label);
    m_pipeline_gauge->SetProgress(info.index * 100 / total);

    if (!m_pipeline_panel->IsShown()) {
        m_pipeline_panel->Show();
        if (m_body)
            m_body->Layout();
    } else {
        m_pipeline_panel->Layout();
    }
}

void OllamaChatPanel::hide_pipeline_progress()
{
    if (!m_pipeline_panel || !m_pipeline_panel->IsShown())
        return;
    m_pipeline_panel->Hide();
    if (m_body)
        m_body->Layout();
}

bool OllamaChatPanel::start_orchestrator_find_and_print(const std::string& query)
{
    if (!AIPipeline::print_job_orchestrator_enabled() || query.empty())
        return false;
    if (!m_orchestrator)
        m_orchestrator = std::make_unique<AIPipeline::PrintJobOrchestrator>();
    if (m_orchestrator->is_active())
        return false;
    return m_orchestrator->start(query, this, m_apply_mode, orchestrator_ui_callbacks());
}

bool OllamaChatPanel::maybe_start_orchestrator_job(nlohmann::json& root, const std::string& user_req)
{
    if (!AIPipeline::print_job_orchestrator_enabled())
        return false;
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;

    bool           started = false;
    nlohmann::json kept    = nlohmann::json::array();
    for (auto& action : root["actions"]) {
        const bool is_find_and_print =
            !started && action.is_object() && action.value("type", std::string{}) == "makerworld_find_and_print";
        if (!is_find_and_print) {
            kept.push_back(action);
            continue;
        }

        if (!m_orchestrator)
            m_orchestrator = std::make_unique<AIPipeline::PrintJobOrchestrator>();
        if (m_orchestrator->is_active()) {
            // A job is already running; leave the action for the legacy path.
            kept.push_back(action);
            continue;
        }

        const std::string query     = action.value("query", std::string{});
        const std::string utterance  = query.empty() ? user_req : query;
        if (m_orchestrator->start(utterance, this, m_apply_mode, orchestrator_ui_callbacks()))
            started = true;
        else
            kept.push_back(action);
    }
    root["actions"] = kept;
    return started;
}

void OllamaChatPanel::set_busy(bool busy)
{
    m_busy = busy;
    if (m_send_btn)
        m_send_btn->Enable(!busy);
    if (m_reset_btn)
        m_reset_btn->Enable(!busy);
    if (m_mode_combo)
        m_mode_combo->Enable(!busy);
    if (m_model_combo)
        m_model_combo->Enable(!busy);
    if (m_input_field)
        m_input_field->Enable(!busy);
    Plater* plater = wxGetApp().plater();
    if (busy) {
        begin_thinking_block();
        // Pending bubble is the in-chat busy indicator; hide the footer line to
        // avoid duplicating "처리 중…" / "요청 처리 중…".
        if (m_status_host) {
            m_status_host->Show(false);
            if (m_body)
                m_body->Layout();
        }
        // Single source of busy truth: the pending bubble. The plater toast is
        // only shown when the chat window itself is not visible.
        if (!IsShownOnScreen())
            OllamaProcessingNotice::show_thinking(plater);
        else
            OllamaProcessingNotice::hide(plater);
    } else {
        // Not busy => no thinking indicator. Clearing here guarantees the
        // pending "처리 중 / Working" bubble disappears on EVERY terminal path
        // (orchestrator finish/error/cancel/timeout, clarify hand-off, legacy
        // flow finish), not just when a chat reply arrives.
        clear_thinking_block();
        if (m_status_host) {
            m_status_host->Show(true);
            if (m_body)
                m_body->Layout();
        }
        OllamaProcessingNotice::hide(plater);
    }
}

OllamaClient::StreamCallback OllamaChatPanel::make_stream_callback(uint64_t gen)
{
    m_stream_buf.clear();
    const auto          alive = m_alive;
    wxWeakRef<wxWindow> weak(this);
    // Called from the client's worker thread per token; marshal to the UI
    // thread and re-validate liveness + request generation before touching UI.
    return [alive, gen, weak](const std::string& chunk) {
        if (!alive->load())
            return;
        wxGetApp().CallAfter([alive, gen, weak, chunk]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            auto* panel = dynamic_cast<OllamaChatPanel*>(weak.get());
            if (!panel || panel->m_request_gen != gen)
                return;
            panel->on_stream_chunk(chunk);
        });
    };
}

void OllamaChatPanel::on_stream_chunk(const std::string& chunk)
{
    if (chunk.empty() || !m_history_list)
        return;
    m_stream_buf += chunk;
    const wxString preview = streaming_preview_text(m_stream_buf);
    if (preview.IsEmpty())
        return;
    if (!m_history_list->has_pending())
        begin_thinking_block();
    m_history_list->set_pending_stream_text(preview);
}

bool OllamaChatPanel::current_plate_has_model() const
{
    if (Plater* plater = wxGetApp().plater()) {
        try {
            return !plater->model().objects.empty();
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "Ollama chat: plate-model check failed; assuming empty plate";
        }
    }
    return false;
}

bool OllamaChatPanel::route_orchestrator_reply(const std::string& user_utf8)
{
    // Phase 3: while an orchestrator job is awaiting a clarifying answer, route
    // this turn to it instead of starting a fresh LLM request.
    if (AIPipeline::print_job_orchestrator_enabled() && m_orchestrator && m_orchestrator->is_active()
        && m_orchestrator->state() == AIPipeline::PrintJobState::Clarifying) {
        m_orchestrator->on_user_reply(user_utf8);
        return true;
    }
    return false;
}

bool OllamaChatPanel::route_garbled_input(const std::string& user_utf8)
{
    if (!ollama_voice_looks_like_garbled_chat(user_utf8))
        return false;
    const wxString msg = AiLocale::text("I couldn't understand that. Try again or type your request.",
                                        "음성/문장을 이해하지 못했습니다. 다시 말씀하시거나 직접 입력해 주세요.");
    append_chat(_L("Assistant"), msg);
    set_status_text(completion_status_for_reply(msg, m_apply_mode));
    return true;
}

bool OllamaChatPanel::route_acquisition(const std::string& user_utf8, bool plate_has_model)
{
    // Deterministic acquisition-intent gate (pre-router): "get me X and print
    // it" (e.g. "용 피규어 출력해줘") starts the end-to-end orchestrator job
    // directly — no LLM, no PrintIntentSession/config-proposal injection this
    // turn. Runs before the MakerWorld bypass so find-and-print requests reach
    // the orchestrator; explicit URL/search requests still use the bypass below.
    if (!OllamaSendRouter::acquisition_gate_open(m_apply_mode, AIPipeline::print_job_orchestrator_enabled(),
                                                 m_orchestrator && m_orchestrator->is_active()))
        return false;
    if (!OllamaSendRouter::is_acquisition_request(user_utf8, plate_has_model))
        return false;
    const std::string query = OllamaSendRouter::acquisition_query(user_utf8);
    if (!start_orchestrator_find_and_print(query))
        return false;
    m_messages.push_back({"user", user_utf8});
    trim_message_history();
    const wxString stub = AiLocale::text("I'll find a model and get the print ready…",
                                         "원하시는 모델을 찾아서 출력을 준비할게요…");
    append_chat(_L("Assistant"), stub);
    m_messages.push_back({"assistant", stub.utf8_string()});
    trim_message_history();
    return true;
}

bool OllamaChatPanel::route_makerworld_bypass(const std::string& user_utf8)
{
    if (!OllamaSendRouter::should_bypass_to_makerworld(user_utf8))
        return false;
    m_messages.push_back({"user", user_utf8});
    trim_message_history();
    const bool     is_import = MakerWorldIntent::user_wants_makerworld_import(user_utf8);
    const wxString stub      = is_import ? _L("Importing from MakerWorld…") : _L("Searching MakerWorld…");
    append_chat(_L("Assistant"), stub);
    m_messages.push_back({"assistant", stub.utf8_string()});
    trim_message_history();
    MakerWorldImportFlow::run_user_makerworld_request(this, user_utf8, m_apply_mode, makerworld_flow_callbacks());
    return true;
}

std::string OllamaChatPanel::build_send_user_message(const wxString& user_text, const std::string& user_utf8)
{
    size_t user_turns = 0;
    for (const auto& m : m_messages)
        if (m.role == "user")
            ++user_turns;

    const bool attach_context =
        (user_turns == 0) || (m_apply_mode ? (user_turns % 2 == 0) : (user_turns % 4 == 0));

    std::string user_msg = user_text.utf8_string();
    if (!m_apply_mode) {
        const bool ko = AiLocale::korean();
        user_msg = ko ? std::string("질문 (설명만, 슬라이서 변경 금지):\n") + user_msg
                      : std::string("Question (explain only — do not change slicer settings):\n") + user_msg;
    }
    // Non-agent apply path: mirror the assist loop so the single-hop request also derives a
    // deterministic proposal that the LLM refines (rather than inventing settings).
    if (m_apply_mode && attach_context) {
        // The deterministic proposal is best-effort context for the LLM. Any failure here
        // (empty/partial config, missing preset, geometry edge cases) must degrade
        // gracefully — an uncaught exception would unwind the wx main loop and quit the app.
        try {
            const bool ko_intent = AiLocale::korean();
            auto&      session   = BambuSmartPrint::PrintIntentSession::instance();
            if (Plater* plater = wxGetApp().plater()) {
                const BambuSmartPrint::PlateContext ctx = PrintPlannerGui::build_plate_context(plater);
                session.merge_turn(user_utf8, ctx.mesh, ctx.base_config);
                OllamaConfigProposalBuilder::build_from_context(plater, ctx, session.intent(), ko_intent);
            } else {
                session.merge_turn(user_utf8);
            }
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(error) << "Ollama proposal build failed: " << ex.what();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Ollama proposal build failed: unknown error";
        }
    }

    if (attach_context) {
        std::string context = (user_turns == 0)
            ? OllamaActionExecutor::build_compact_context_json()
            : OllamaActionExecutor::build_context_json();
        if (m_apply_mode) {
            try {
                nlohmann::json ctx = nlohmann::json::parse(context);
                const bool     ko  = AiLocale::korean();
                ctx["pro_tips"]    = OllamaPrintingTips::tips_for_request(user_utf8, ko);
                context            = ctx.dump(2);
            } catch (...) {
                BOOST_LOG_TRIVIAL(warning) << "Ollama chat: pro_tips context enrichment failed; sending base context";
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
            BOOST_LOG_TRIVIAL(warning) << "Ollama chat: config digest failed; sending message without it";
        }
        if (OllamaAgentGoalPlanner::is_single_shot_apply(user_utf8)) {
            const bool           ko   = AiLocale::korean();
            const nlohmann::json hint = OllamaAgentGoalPlanner::build_plan_hint(user_utf8, ko);
            user_msg += std::string("\n\nApply plan hint (JSON):\n") + hint.dump(2);
        }
    }
    return user_msg;
}

bool OllamaChatPanel::route_assist_loop(const std::string& user_msg, const std::string& user_utf8, bool plate_has_model)
{
    if (!OllamaSendRouter::should_use_assist_loop(user_utf8, m_apply_mode, plate_has_model))
        return false;
    m_messages.push_back({"user", user_msg});
    trim_message_history();
    start_assist_loop_turn(user_utf8);
    return true;
}

void OllamaChatPanel::dispatch_single_shot_chat(const std::string& user_msg, const std::string& user_utf8)
{
    m_messages.push_back({"user", user_msg});
    trim_message_history();

    set_busy(true);
    const bool ko_fast = AiLocale::korean();
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
    }, OllamaRequestKind::Chat, make_stream_callback(gen));
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

    const std::string user_utf8       = user_text.utf8_string();
    const bool        plate_has_model = current_plate_has_model();

    // Ordered routing: the first handler that fully processes the turn wins.
    if (route_orchestrator_reply(user_utf8))
        return;
    if (route_garbled_input(user_utf8))
        return;
    if (route_acquisition(user_utf8, plate_has_model))
        return;
    if (route_makerworld_bypass(user_utf8))
        return;

    std::string user_msg = build_send_user_message(user_text, user_utf8);

    if (!m_available_models.empty())
        m_model = resolve_installed_model(m_available_models, m_model);

    if (route_assist_loop(user_msg, user_utf8, plate_has_model))
        return;
    dispatch_single_shot_chat(user_msg, user_utf8);
}

void OllamaChatPanel::start_assist_loop_turn(const std::string& user_utf8)
{
    set_busy(true);
    const bool ko = AiLocale::korean();
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

    const bool ko = AiLocale::korean();
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
    set_status_text(AiLocale::korean() ? wxString::FromUTF8("다시 시도 중…") : _L("Retrying…"));
    append_thinking_line(AiLocale::korean() ? wxString::FromUTF8("다시 시도 중…") : _L("Retrying…"));

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
    }, OllamaRequestKind::Chat, make_stream_callback(gen));
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
    set_status_text(AiLocale::korean()
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
    set_status_text(AiLocale::korean() ? wxString::FromUTF8("모델 다운로드 중…") : _L("Downloading model…"));
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

        // Phase 3 (flag-gated): a full "find and print" intent is driven end-to-end
        // by the PrintJobOrchestrator. Consumed actions are removed from `root`.
        if (maybe_start_orchestrator_job(root, user_req)) {
            if (!root.contains("actions") || !root["actions"].is_array() || root["actions"].empty()) {
                append_chat(_L("Assistant"), display);
                return;
            }
        }

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
        if (m_apply_mode && MakerWorldIntent::is_pure_makerworld_request(user_req)) {
            const bool is_import = MakerWorldIntent::user_wants_makerworld_import(user_req);
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

