#include "BambuSmartPrintDeviceSummary.hpp"
#include "BambuSmartPrintService.hpp"
#include "BambuSmartPrintUi.hpp"

#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/Label.hpp"
#include "libslic3r/BambuSmartPrint/FailureDatabase.hpp"
#include "libslic3r/BambuSmartPrint/PrinterLearningStore.hpp"

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

namespace {

void append_summary_part(wxString& text, const wxString& part)
{
    if (part.empty())
        return;
    if (!text.empty())
        text << " | ";
    text << part;
}

} // namespace

BambuSmartPrintDeviceSummary::BambuSmartPrintDeviceSummary(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    apply_panel_chrome(this);
    auto* root = new wxBoxSizer(wxVERTICAL);

    m_failure_banner = create_banner(this, &m_failure_text, BannerKind::Warning);
    m_failure_btn = new Button(m_failure_banner, _L("View"));
    style_primary_button(m_failure_btn);
    size_action_button(m_failure_banner, m_failure_btn, 30);
    if (wxSizer* fail_sz = m_failure_banner->GetSizer())
        fail_sz->Add(m_failure_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    m_failure_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        BambuSmartPrintService::instance().flush_pending_failure_dialog();
        wxGetApp().open_smart_print();
    });
    m_failure_banner->Hide();
    root->Add(m_failure_banner, 0, wxEXPAND | wxALL, FromDIP(8));

    wxBoxSizer* inner = nullptr;
    wxPanel* body = nullptr;
    auto* card = create_card(this, &inner, &body, 10);
    add_card_section_title(body, inner, _L("Smart Print"), _L("Per-printer learning from local history"));

    m_summary = new wxStaticText(body, wxID_ANY,
        _L("Select a printer to see learning stats from past prints."),
        wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    style_body_text(m_summary, true);
    wrap_static_text(m_summary, this, 520);
    inner->Add(m_summary, 0, wxEXPAND);

    root->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    SetSizer(root);
}

void BambuSmartPrintDeviceSummary::update_for_printer(const std::string& printer_id)
{
    const size_t pending = BambuSmartPrintService::instance().pending_failure_count();
    if (m_failure_banner) {
        if (pending > 0 && BambuSmartPrintService::is_enabled()) {
            if (m_failure_text) {
                m_failure_text->SetLabel(pending == 1
                    ? _L("Smart Print has diagnosis and fix suggestions for a recent print failure.")
                    : wxString::Format(_L("%zu print failures queued — open Smart Print to review."),
                                       pending));
            }
            m_failure_banner->Show();
        } else {
            m_failure_banner->Hide();
        }
    }

    if (!BambuSmartPrintService::is_enabled()) {
        m_summary->SetLabel(_L("Smart Print is off — enable it under Preferences → Smart Print or the Prepare bar."));
        wrap_static_text(m_summary, this, 520);
        Layout();
        return;
    }
    if (!BambuSmartPrintService::is_bbl_printer_active()) {
        m_summary->SetLabel(_L("Select a Bambu Lab printer profile to use Smart Print on this device."));
        wrap_static_text(m_summary, this, 520);
        Layout();
        return;
    }
    if (printer_id.empty()) {
        m_summary->SetLabel(_L("Select a printer to see learning stats."));
        wrap_static_text(m_summary, this, 520);
        Layout();
        return;
    }

    const int fails = BambuSmartPrint::FailureDatabase::instance().count_failures(printer_id);
    const int recent = BambuSmartPrint::FailureDatabase::instance().count_failures_recent(printer_id);
    const int ok = BambuSmartPrint::FailureDatabase::instance().count_successes(printer_id);
    const auto profile = BambuSmartPrint::PrinterLearningStore::instance().get_profile(printer_id);

    wxString text;
    if (profile.total_prints > 0) {
        const int rate = int(std::round(100.f * float(profile.successful_prints) / float(profile.total_prints)));
        text = wxString::Format(_L("History: %d%% success (%d/%d jobs)"),
                                rate, profile.successful_prints, profile.total_prints);
    }

    wxString stats = wxString::Format(_L_PLURAL("%d success", "%d successes", ok), ok);
    stats << ", " << wxString::Format(_L_PLURAL("%d failure", "%d failures", fails), fails);
    stats << wxString::Format(_L(" (%d in last 30 days)"), recent);
    append_summary_part(text, stats);

    int adhesion = 0;
    if (auto it = profile.failures_by_category.find("adhesion"); it != profile.failures_by_category.end())
        adhesion = it->second;
    if (adhesion >= 2)
        append_summary_part(text, wxString::Format(_L("Adhesion learning active (%d)"), adhesion));

    if (!profile.setting_adjustments.empty()) {
        const size_t tuned = profile.setting_adjustments.size();
        append_summary_part(text,
            wxString::Format(_L_PLURAL("%zu tuned setting", "%zu tuned settings", tuned), tuned));
    }

    m_summary->SetLabel(text);
    wrap_static_text(m_summary, this, 520);
    Layout();
}

}} // namespace
