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

// The stored value is the notice version the user agreed to. AppConfig turns
// "1" into JSON true, which reads back as "true": that was version 1.
bool stored_consent(AppConfig* cfg)
{
    if (!cfg)
        return false;
    std::string v = cfg->get(kOllamaConfigSection, kConsentKey);
    if (v == "true")
        v = "1";
    return v == kAiPrivacyConsentVersion;
}
}

bool ai_privacy_consent_given() { return g_consent.load(); }

void load_ai_privacy_consent()
{
    AppConfig* cfg = wxGetApp().app_config;
    g_consent = stored_consent(cfg);
}

bool ensure_ai_privacy_consent(wxWindow* parent)
{
    AppConfig* cfg = wxGetApp().app_config;
    if (stored_consent(cfg)) {
        g_consent = true;
        return true;
    }

    const wxString body = _L(
        "The AI assistant uses OpenAI's ChatGPT. When you use it, the following is sent to OpenAI under your ChatGPT account:\n\n"
        "• What you type or say\n"
        "• Plate state: model names, sizes and rotation; current printer, filament and print settings\n"
        "• Printer error codes and alerts (when you ask about a failed print)\n\n"
        "Model files (STL/3MF) and G-code are not sent. Voice input uses macOS speech recognition. "
        "Your sign-in is stored only on this Mac, in a file only your user account can read; there is no VerSlicer server.\n\n"
        "Agree and use the AI assistant?");
    wxMessageDialog dlg(parent, body, _L("AI assistant and your data"), wxYES_NO | wxNO_DEFAULT | wxICON_INFORMATION);
    dlg.SetYesNoLabels(_L("Agree"), _L("Cancel"));
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
