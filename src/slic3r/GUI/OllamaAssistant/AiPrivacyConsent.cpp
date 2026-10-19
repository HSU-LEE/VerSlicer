#include "AiPrivacyConsent.hpp"

#include "AiLocale.hpp"
#include "OllamaConfig.hpp"
#include "../GUI_App.hpp"

#include <wx/msgdlg.h>

#include <atomic>

namespace Slic3r { namespace GUI {

namespace {
constexpr const char* kConsentKey = "privacy_consent";
std::atomic<bool>     g_consent{false};
}

bool ai_privacy_consent_given() { return g_consent.load(); }

void load_ai_privacy_consent()
{
    AppConfig* cfg = wxGetApp().app_config;
    g_consent = cfg && cfg->get(kOllamaConfigSection, kConsentKey) == kAiPrivacyConsentVersion;
}

bool ensure_ai_privacy_consent(wxWindow* parent)
{
    AppConfig* cfg = wxGetApp().app_config;
    if (cfg && cfg->get(kOllamaConfigSection, kConsentKey) == kAiPrivacyConsentVersion) {
        g_consent = true;
        return true;
    }

    const bool ko = AiLocale::korean();
    const wxString body = ko
        ? wxString::FromUTF8(
              "AI 도우미는 OpenAI의 ChatGPT를 사용합니다. 사용하면 다음 정보가 내 ChatGPT 계정으로 OpenAI에 전송됩니다.\n\n"
              "• 입력하거나 말한 요청 문장\n"
              "• 플레이트 상태: 모델 이름·크기·회전, 현재 프린터·필라멘트·출력 설정\n"
              "• 프린터 오류 코드와 알림(출력 실패를 물어볼 때)\n\n"
              "모델 파일(STL/3MF) 자체와 G-code는 보내지 않습니다. 음성 입력은 macOS 음성 인식을 사용합니다. "
              "로그인 정보는 이 Mac의 키체인에만 저장되며 VerSlicer 서버는 없습니다.\n\n"
              "동의하고 AI 도우미를 사용할까요?")
        : wxString(
              "The AI assistant uses OpenAI's ChatGPT. When you use it, the following is sent to OpenAI under your ChatGPT account:\n\n"
              "• What you type or say\n"
              "• Plate state: model names, sizes and rotation; current printer, filament and print settings\n"
              "• Printer error codes and alerts (when you ask about a failed print)\n\n"
              "Model files (STL/3MF) and G-code are not sent. Voice input uses macOS speech recognition. "
              "Your sign-in is stored only in this Mac's Keychain; there is no VerSlicer server.\n\n"
              "Agree and use the AI assistant?");
    wxMessageDialog dlg(parent, body, ko ? wxString::FromUTF8("AI 도우미 개인정보 안내") : wxString("AI assistant and your data"),
                        wxYES_NO | wxNO_DEFAULT | wxICON_INFORMATION);
    dlg.SetYesNoLabels(ko ? wxString::FromUTF8("동의") : wxString("Agree"), ko ? wxString::FromUTF8("취소") : wxString("Cancel"));
    if (dlg.ShowModal() != wxID_YES)
        return false;
    if (cfg) {
        cfg->set(kOllamaConfigSection, kConsentKey, kAiPrivacyConsentVersion);
        cfg->save();
    }
    g_consent = true;
    return true;
}

}} // namespace
