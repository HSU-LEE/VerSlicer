#include "MakerWorldPrintOfferDialog.hpp"

#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../OllamaAssistant/AiLocale.hpp"
#include "../Widgets/DialogButtons.hpp"
#include "../Widgets/Label.hpp"

#include "slic3r/Utils/Http.hpp"

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include <algorithm>

namespace Slic3r { namespace GUI {

namespace {

constexpr int kThumbDip = 56;

wxString elide(const wxString& s, size_t max_chars)
{
    if (s.length() <= max_chars)
        return s;
    return s.Left(static_cast<long>(max_chars) - 1) + wxT("…");
}

// Neutral placeholder shown until (or instead of) the downloaded cover image.
wxBitmap placeholder_bitmap(wxWindow* ref)
{
    const int px = ref->FromDIP(kThumbDip);
    wxImage   img(px, px);
    const wxColour c = SlicePilotUi::Theme::surface_alt();
    img.SetRGB(wxRect(0, 0, px, px), c.Red(), c.Green(), c.Blue());
    return wxBitmap(img);
}

} // namespace

MakerWorldPrintOfferDialog::MakerWorldPrintOfferDialog(wxWindow* parent,
                                                       const std::vector<MakerWorldCandidate>& candidates)
    : DPIDialog(parent, wxID_ANY,
                AiLocale::text(_L("MakerWorld print"), "MakerWorld 출력"),
                wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_candidates(candidates)
    , m_alive(std::make_shared<std::atomic<bool>>(true))
{
    SlicePilotUi::apply_dialog_chrome(this);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(SlicePilotUi::create_header(this,
        AiLocale::text(_L("Choose a model to print"), "출력할 모델을 선택하세요"),
        AiLocale::text(_L("Models found on MakerWorld. The highlighted model prints automatically when the timer ends."),
                       "MakerWorld에서 찾은 모델입니다. 시간이 지나면 선택된 모델로 자동 진행됩니다.")),
        0, wxEXPAND);

    const int pad = FromDIP(12);
    auto* body = new wxBoxSizer(wxVERTICAL);
    for (size_t i = 0; i < m_candidates.size(); ++i)
        body->Add(build_candidate_card(this, m_candidates[i], static_cast<int>(i)),
                  0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    root->Add(body, 1, wxEXPAND | wxBOTTOM, pad);

    root->Add(new wxStaticLine(this), 0, wxEXPAND);
    auto* buttons = new DialogButtons(this, {"cancel", "ok"});
    m_confirm_btn = buttons->GetOK();
    if (Button* cancel = buttons->GetCANCEL()) {
        cancel->SetLabel(AiLocale::text(_L("Cancel"), "취소"));
        cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            m_timer.Stop();
            m_selected_index = -1;
            EndModal(wxID_CANCEL);
        });
    }
    if (m_confirm_btn) {
        m_confirm_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
        m_confirm_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { confirm(); });
    }
    root->Add(buttons, 0, wxEXPAND);
    SetEscapeId(wxID_CANCEL);

    SetSizer(root);

    m_selected_index = m_candidates.empty() ? -1 : 0;
    select(m_selected_index);
    update_confirm_label();

    SlicePilotUi::finalize_modal_dialog(this, wxSize(440, 260), wxSize(500, 340));
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);

    // Keyboard: 1..3 select, Enter confirms (Esc handled via SetEscapeId).
    Bind(wxEVT_CHAR_HOOK, &MakerWorldPrintOfferDialog::on_char_hook, this);

    m_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &MakerWorldPrintOfferDialog::on_timer, this);
    m_timer.Start(1000);

    // Cover images load off-thread; the card keeps its placeholder on failure.
    for (size_t i = 0; i < m_candidates.size(); ++i) {
        if (!m_candidates[i].cover_url.empty())
            start_thumbnail_fetch(static_cast<int>(i), m_candidates[i].cover_url);
    }
}

MakerWorldPrintOfferDialog::~MakerWorldPrintOfferDialog()
{
    if (m_alive)
        m_alive->store(false);
    m_timer.Stop();
}

void MakerWorldPrintOfferDialog::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    Layout();
}

wxPanel* MakerWorldPrintOfferDialog::build_candidate_card(wxWindow* parent, const MakerWorldCandidate& c, int index)
{
    using SlicePilotUi::Theme;

    // 1px "frame" panel doubles as the selection highlight (border → primary).
    auto* frame = new wxPanel(parent, wxID_ANY);
    frame->SetBackgroundColour(Theme::border());

    auto* card = new wxPanel(frame, wxID_ANY);
    card->SetBackgroundColour(Theme::background());

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    const int pad = FromDIP(10);

    auto* thumb = new wxStaticBitmap(card, wxID_ANY, placeholder_bitmap(this));
    thumb->SetMinSize(wxSize(FromDIP(kThumbDip), FromDIP(kThumbDip)));
    row->Add(thumb, 0, wxALIGN_CENTER_VERTICAL | wxALL, pad);

    auto* text_col = new wxBoxSizer(wxVERTICAL);
    auto* title = new wxStaticText(card, wxID_ANY,
        wxString::Format("%d. %s", index + 1, wxString::FromUTF8(c.title)),
        wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    title->SetFont(Label::Head_14);
    title->SetForegroundColour(Theme::text());
    title->SetBackgroundColour(Theme::background());
    text_col->Add(title, 0, wxEXPAND);
    if (!c.author.empty()) {
        auto* author = new wxStaticText(card, wxID_ANY, wxString::FromUTF8(c.author),
                                        wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        SlicePilotUi::style_body_text(author, /*muted=*/true);
        author->SetBackgroundColour(Theme::background());
        text_col->Add(author, 0, wxEXPAND | wxTOP, FromDIP(2));
    }
    row->Add(text_col, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
    card->SetSizer(row);

    auto* frame_sz = new wxBoxSizer(wxVERTICAL);
    const int border = std::max(1, FromDIP(1));
    frame_sz->Add(card, 1, wxEXPAND | wxALL, border);
    frame->SetSizer(frame_sz);

    // Whole card (and every child) is clickable.
    auto on_click = [this, index](wxMouseEvent&) { select(index); };
    frame->Bind(wxEVT_LEFT_DOWN, on_click);
    card->Bind(wxEVT_LEFT_DOWN, on_click);
    thumb->Bind(wxEVT_LEFT_DOWN, on_click);
    title->Bind(wxEVT_LEFT_DOWN, on_click);
    for (wxWindow* child : card->GetChildren())
        child->Bind(wxEVT_LEFT_DOWN, on_click);

    CandidateCard entry;
    entry.frame = frame;
    entry.thumb = thumb;
    m_cards.push_back(entry);
    return frame;
}

void MakerWorldPrintOfferDialog::select(int index)
{
    if (index < 0 || index >= static_cast<int>(m_candidates.size()))
        return;
    m_selected_index = index;
    for (size_t i = 0; i < m_cards.size(); ++i) {
        if (!m_cards[i].frame)
            continue;
        m_cards[i].frame->SetBackgroundColour(static_cast<int>(i) == index
                                                  ? SlicePilotUi::Theme::primary()
                                                  : SlicePilotUi::Theme::border());
        m_cards[i].frame->Refresh();
    }
    update_confirm_label();
}

void MakerWorldPrintOfferDialog::confirm()
{
    if (m_selected_index < 0 && !m_candidates.empty())
        m_selected_index = 0;
    m_timer.Stop();
    EndModal(wxID_OK);
}

void MakerWorldPrintOfferDialog::on_timer(wxTimerEvent&)
{
    --m_seconds_left;
    if (m_seconds_left <= 0) {
        confirm();
        return;
    }
    update_confirm_label();
}

void MakerWorldPrintOfferDialog::on_char_hook(wxKeyEvent& evt)
{
    const int code = evt.GetKeyCode();
    if (code >= '1' && code <= '9') {
        const int idx = code - '1';
        if (idx < static_cast<int>(m_candidates.size())) {
            select(idx);
            return;
        }
    } else if (code == WXK_RETURN || code == WXK_NUMPAD_ENTER) {
        confirm();
        return;
    }
    evt.Skip();
}

void MakerWorldPrintOfferDialog::update_confirm_label()
{
    if (!m_confirm_btn)
        return;
    wxString title;
    if (m_selected_index >= 0 && m_selected_index < static_cast<int>(m_candidates.size()))
        title = elide(wxString::FromUTF8(m_candidates[static_cast<size_t>(m_selected_index)].title), 24);
    wxString label;
    if (title.IsEmpty())
        label = AiLocale::text(_L("Start print"), "출력 시작");
    else if (m_seconds_left > 0)
        label = AiLocale::korean()
            ? wxString::Format(wxString::FromUTF8("'%s' 출력 (%d초)"), title, m_seconds_left)
            : wxString::Format(_L("Print \"%s\" (%ds)"), title, m_seconds_left);
    else
        label = AiLocale::korean()
            ? wxString::Format(wxString::FromUTF8("'%s' 출력"), title)
            : wxString::Format(_L("Print \"%s\""), title);
    m_confirm_btn->SetLabel(label);
    if (wxWindow* parent = m_confirm_btn->GetParent())
        parent->Layout();
    Layout();
}

void MakerWorldPrintOfferDialog::start_thumbnail_fetch(int index, const std::string& url)
{
    auto alive = m_alive;
    // Http::perform() runs on a worker thread; the completion marshals back to
    // the UI thread and re-validates the dialog before touching widgets.
    Http::get(url)
        .timeout_connect(5)
        .timeout_max(15)
        .on_complete([this, alive, index](std::string body, unsigned status) {
            if (!alive->load() || status < 200 || status >= 300 || body.empty())
                return;
            wxGetApp().CallAfter([this, alive, index, body = std::move(body)]() {
                if (!alive->load() || wxGetApp().is_closing())
                    return;
                if (index < 0 || index >= static_cast<int>(m_cards.size()) || !m_cards[index].thumb)
                    return;
                wxMemoryInputStream mis(body.data(), body.size());
                wxImage img(mis);
                if (!img.IsOk())
                    return; // unsupported format — keep the placeholder
                const int px = FromDIP(kThumbDip);
                // Cover-fit: scale the shorter side to the box, center-crop the rest.
                const int    w     = img.GetWidth();
                const int    h     = img.GetHeight();
                const double scale = std::max(static_cast<double>(px) / w, static_cast<double>(px) / h);
                img.Rescale(std::max(1, static_cast<int>(w * scale + 0.5)),
                            std::max(1, static_cast<int>(h * scale + 0.5)), wxIMAGE_QUALITY_HIGH);
                const int x = std::max(0, (img.GetWidth() - px) / 2);
                const int y = std::max(0, (img.GetHeight() - px) / 2);
                img = img.GetSubImage(wxRect(x, y, std::min(px, img.GetWidth()), std::min(px, img.GetHeight())));
                m_cards[index].thumb->SetBitmap(wxBitmap(img));
                m_cards[index].thumb->Refresh();
                Layout();
            });
        })
        .perform();
}

MakerWorldCandidate MakerWorldPrintOfferDialog::selected_candidate() const
{
    if (m_selected_index >= 0 && m_selected_index < static_cast<int>(m_candidates.size()))
        return m_candidates[static_cast<size_t>(m_selected_index)];
    return {};
}

}} // namespace
