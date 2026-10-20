#include "ChatGptAccountBar.hpp"

#include "../UiDispatch.hpp"
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
    m_button = new Button(parent, _L("Connect ChatGPT"));
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
        m_label->SetLabel(email.empty() ? _L("ChatGPT connected") : wxString::FromUTF8(email));
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
        m_button->SetLabel(_L("Sign out"));
        m_button->SetToolTip(_L("Remove the ChatGPT connection on this Mac."));
        m_button->SetMinSize(wxSize(m_parent->FromDIP(64), m_parent->FromDIP(28)));
    } else if (!m_signing_in) {
        SlicePilotUi::style_primary_button(m_button);
        m_button->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
        m_button->SetFont(Label::Body_14);
        m_button->SetLabel(_L("Connect ChatGPT"));
        m_button->SetToolTip(_L("Opens the browser. Sign in to ChatGPT and allow access."));
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
    wxMessageDialog confirm(m_parent, _L("Remove the saved ChatGPT connection on this Mac?"),
                            _L("Sign out of ChatGPT"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
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
    m_button->SetLabel(_L("Waiting for the browser…"));
    m_button->Enable(false);
    if (m_cb.notify)
        m_cb.notify(_L("The browser will open. Sign in to ChatGPT and allow VerSlicer. Come back here when it finishes."));
    const auto alive = m_alive;
    std::thread([this, alive]() {
        const std::string error = ChatGptAccount::sign_in();
        post_to_ui([this, alive, error]() {
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
