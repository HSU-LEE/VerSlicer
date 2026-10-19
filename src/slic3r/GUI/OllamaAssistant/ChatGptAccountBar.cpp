#include "ChatGptAccountBar.hpp"

#include "AiLocale.hpp"
#include "AiPrivacyConsent.hpp"
#include "ChatGptAccount.hpp"
#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../GUI_App.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/Label.hpp"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <thread>

namespace Slic3r { namespace GUI {

namespace {

wxString ko_or(const char* ko_utf8, const char* en)
{
    return AiLocale::korean() ? wxString::FromUTF8(ko_utf8) : wxString(en);
}

} // namespace

ChatGptAccountBar::ChatGptAccountBar(wxWindow* parent, int row_height, Callbacks callbacks)
    : m_parent(parent), m_cb(std::move(callbacks)), m_alive(std::make_shared<std::atomic<bool>>(true))
{
    m_label = new wxStaticText(parent, wxID_ANY, wxEmptyString);
    m_label->SetForegroundColour(SlicePilotUi::Theme::text());
    m_label->SetFont(Label::Body_13);
    m_label->Hide();
    m_button = new Button(parent, ko_or("ChatGPT 계정 연결", "Connect ChatGPT"));
    SlicePilotUi::style_primary_button(m_button);
    m_button->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
    m_button->SetFont(Label::Body_14);
    m_button->SetMinSize(wxSize(parent->FromDIP(160), row_height));
    m_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { toggle(); });

    m_row = new wxBoxSizer(wxHORIZONTAL);
    m_row->Add(m_label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(6));
    m_row->Add(m_button, 1, wxEXPAND);
}

ChatGptAccountBar::~ChatGptAccountBar()
{
    // A sign-in thread may still be waiting on the browser; it must not touch us.
    m_alive->store(false);
}

void ChatGptAccountBar::set_busy(bool busy)
{
    m_busy = busy;
    m_button->Enable(!busy && !m_signing_in);
}

void ChatGptAccountBar::refresh()
{
    const bool linked = ChatGptAccount::linked();
    if (linked) {
        const std::string email = ChatGptAccount::email();
        m_label->SetLabel(email.empty() ? ko_or("ChatGPT 연결됨", "ChatGPT connected") : wxString::FromUTF8(email));
        m_label->Show();
    } else {
        m_label->Hide();
    }
    if (auto* item = m_row->GetItem(m_button))
        item->SetProportion(linked ? 0 : 1);
    if (linked) {
        SlicePilotUi::style_secondary_button(m_button);
        m_button->SetStyle(ButtonStyle::Regular, ButtonType::Compact);
        m_button->SetFont(Label::Body_13);
        m_button->SetLabel(ko_or("해제", "Sign out"));
        m_button->SetToolTip(ko_or("이 Mac에서 ChatGPT 연결을 지웁니다.", "Remove the ChatGPT connection on this Mac."));
        m_button->SetMinSize(wxSize(m_parent->FromDIP(64), m_parent->FromDIP(28)));
    } else if (!m_signing_in) {
        SlicePilotUi::style_primary_button(m_button);
        m_button->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
        m_button->SetFont(Label::Body_14);
        m_button->SetLabel(ko_or("ChatGPT 계정 연결", "Connect ChatGPT"));
        m_button->SetToolTip(ko_or("브라우저가 열립니다. ChatGPT에 로그인하고 허용을 누르세요.",
                                   "Opens the browser. Sign in to ChatGPT and allow access."));
        m_button->SetMinSize(wxSize(m_parent->FromDIP(160), m_parent->FromDIP(28)));
    }
    if (m_cb.relayout)
        m_cb.relayout();
}

void ChatGptAccountBar::toggle()
{
    if (m_busy || m_signing_in)
        return;
    if (!ChatGptAccount::linked()) {
        start_sign_in();
        return;
    }
    wxMessageDialog confirm(m_parent, ko_or("이 Mac에 저장된 ChatGPT 연결을 지울까요?", "Remove the saved ChatGPT connection on this Mac?"),
                            ko_or("ChatGPT 연결 해제", "Sign out of ChatGPT"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    if (confirm.ShowModal() != wxID_YES)
        return;
    ChatGptAccount::sign_out();
    refresh();
    if (m_cb.signed_out)
        m_cb.signed_out();
}

void ChatGptAccountBar::start_sign_in()
{
    if (!ensure_ai_privacy_consent(m_parent))
        return;
    m_signing_in = true;
    m_button->SetLabel(ko_or("브라우저에서 로그인 중…", "Waiting for the browser…"));
    m_button->Enable(false);
    if (m_cb.notify)
        m_cb.notify(ko_or("브라우저가 열립니다. ChatGPT에 로그인하고, VerSlicer 사용 허용을 누르세요. 이 창으로 돌아오면 연결이 끝납니다.",
                          "The browser will open. Sign in to ChatGPT and allow VerSlicer. Come back here when it finishes."));
    const auto alive = m_alive;
    std::thread([this, alive]() {
        const std::string error = ChatGptAccount::sign_in();
        wxGetApp().CallAfter([this, alive, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            finish_sign_in(error);
        });
    }).detach();
}

void ChatGptAccountBar::finish_sign_in(const std::string& error)
{
    m_signing_in = false;
    m_button->Enable(!m_busy);
    refresh();
    if (!error.empty()) {
        if (m_cb.notify)
            m_cb.notify(m_cb.format_error ? m_cb.format_error(error) : wxString::FromUTF8(error));
        return;
    }
    if (m_cb.signed_in)
        m_cb.signed_in();
}

}} // namespace
