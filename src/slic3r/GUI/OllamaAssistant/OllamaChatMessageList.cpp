#include "OllamaChatMessageList.hpp"

#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../Widgets/Label.hpp"
#include "../Widgets/StaticBox.hpp"

#include <wx/dcclient.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

#include <algorithm>

namespace Slic3r { namespace GUI {

namespace {

constexpr size_t kMaxMessages = 200;
constexpr int    kPendingAnimMs = 400;

constexpr int kBubblePadXDp = 12;
constexpr int kBubblePadYDp = 8;
constexpr int kProgressPadYDp = 4;
constexpr int kListPadDp = 8;
// macOS draws the vertical scrollbar over the client edge; reserve even before
// FitInside reports overflow so right-aligned bubbles never clip.
constexpr int kScrollbarReserveDp = 14;
constexpr int kUserRightMarginDp  = 6;

int bubble_pad_y_dp(ChatMessageKind kind)
{
    return kind == ChatMessageKind::Progress ? kProgressPadYDp : kBubblePadYDp;
}

int bubble_border_px(wxWindow* ref)
{
    return std::max(1, ref->FromDIP(1));
}

wxSize measure_wrapped_label(wxStaticText* label, int wrap_px, wxWindow* ref)
{
    label->Wrap(wrap_px);
    const wxSize best = label->GetBestSize();
    wxClientDC     dc(label);
    dc.SetFont(label->GetFont());
    const wxSize dc_sz = dc.GetMultiLineTextExtent(label->GetLabel());
    const int    slack = ref->FromDIP(2);
    const int    w     = std::min(std::max(best.GetWidth(), dc_sz.GetWidth()) + slack, wrap_px);
    const int    h     = std::max(best.GetHeight(), dc_sz.GetHeight()) + slack;
    return wxSize(w, h);
}

} // namespace

OllamaChatMessageList::OllamaChatMessageList(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE)
{
    SetBackgroundColour(SlicePilotUi::Theme::background());
    SetScrollRate(0, FromDIP(10));
    m_sizer = new wxBoxSizer(wxVERTICAL);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(m_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(kListPadDp));
    outer->AddSpacer(FromDIP(kListPadDp));
    SetSizer(outer);

    Bind(wxEVT_SIZE, &OllamaChatMessageList::on_size, this);

    m_pending_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { on_pending_timer(); });
}

OllamaChatMessageList::~OllamaChatMessageList()
{
    if (m_pending_timer) {
        m_pending_timer->Stop();
        m_pending_timer = nullptr;
    }
}

int OllamaChatMessageList::viewport_content_width() const
{
    int client = GetClientSize().GetWidth();
    if (client < FromDIP(120))
        return FromDIP(320);
    client -= 2 * FromDIP(kListPadDp);
    client -= FromDIP(kScrollbarReserveDp);
    return std::max(FromDIP(120), client);
}

int OllamaChatMessageList::bubble_wrap_width() const
{
    const int content = viewport_content_width();
    return std::max(FromDIP(160), content * 70 / 100 - FromDIP(2 * kBubblePadXDp));
}

int OllamaChatMessageList::max_bubble_width() const
{
    const int wrap    = bubble_wrap_width();
    const int pad_x   = FromDIP(kBubblePadXDp);
    const int border  = bubble_border_px(const_cast<OllamaChatMessageList*>(this));
    return wrap + 2 * pad_x + 2 * border;
}

int OllamaChatMessageList::row_align_flags(ChatMessageRole role, ChatMessageKind kind) const
{
    if (kind == ChatMessageKind::Progress)
        return wxALIGN_CENTER_HORIZONTAL | wxTOP;
    if (role == ChatMessageRole::User)
        return wxALIGN_RIGHT | wxTOP;
    return wxALIGN_LEFT | wxTOP;
}

void OllamaChatMessageList::style_bubble(const Row& row)
{
    using SlicePilotUi::Theme;
    if (!row.bubble || !row.label)
        return;

    wxColour bg     = Theme::surface();
    wxColour border = Theme::border();
    wxColour fg     = Theme::text();
    wxFont   font   = Label::Body_14;

    switch (row.role) {
    case ChatMessageRole::User:
        bg     = Theme::primary_soft();
        border = Theme::primary_soft();
        break;
    case ChatMessageRole::System:
        bg     = Theme::surface_alt();
        border = Theme::surface_alt();
        fg     = Theme::text_muted();
        font   = Label::Body_13;
        break;
    case ChatMessageRole::Assistant:
        break;
    }
    switch (row.kind) {
    case ChatMessageKind::Progress:
        bg     = Theme::background();
        border = Theme::background();
        fg     = Theme::text_muted();
        font   = Label::Body_12;
        break;
    case ChatMessageKind::Question:
        bg     = Theme::surface();
        border = Theme::primary();
        break;
    case ChatMessageKind::Error:
        bg     = Theme::surface();
        border = Theme::warning();
        break;
    case ChatMessageKind::Normal:
        break;
    }

    row.bubble->SetBackgroundColorNormal(bg);
    row.bubble->SetBorderColorNormal(border);
    row.bubble->SetCornerRadius(FromDIP(8));
    row.label->SetBackgroundColour(bg);
    row.label->SetForegroundColour(fg);
    row.label->SetFont(font);
}

OllamaChatMessageList::Row OllamaChatMessageList::make_row(ChatMessageRole role, ChatMessageKind kind,
                                                           const wxString& text, bool pending)
{
    Row row;
    row.role = role;
    row.kind = kind;
    row.text = text;
    row.id   = pending ? -1 : m_next_id++;

    row.row_panel = new wxPanel(this, wxID_ANY);
    row.row_panel->SetBackgroundColour(SlicePilotUi::Theme::background());
    auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);

    row.bubble = new StaticBox(row.row_panel, wxID_ANY);
    row.bubble->SetBorderWidth(std::max(1, FromDIP(1)));
    const int pad_x = FromDIP(kBubblePadXDp);
    const int pad_y = FromDIP(bubble_pad_y_dp(kind));
    row.label = new wxStaticText(row.bubble, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxST_NO_AUTORESIZE);
    auto* bubble_col = new wxBoxSizer(wxVERTICAL);
    bubble_col->AddSpacer(pad_y);
    bubble_col->Add(row.label, 0, wxLEFT | wxRIGHT, pad_x);
    bubble_col->AddSpacer(pad_y);
    row.bubble->SetSizer(bubble_col);

    if (kind == ChatMessageKind::Progress && !pending) {
        row_sizer->AddStretchSpacer(1);
        row_sizer->Add(row.bubble, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->AddStretchSpacer(1);
    } else if (role == ChatMessageRole::User) {
        row_sizer->AddStretchSpacer(1);
        row_sizer->Add(row.bubble, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->AddSpacer(FromDIP(kUserRightMarginDp));
    } else {
        row_sizer->AddSpacer(FromDIP(4));
        row_sizer->Add(row.bubble, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->AddStretchSpacer(1);
    }
    row.row_panel->SetSizer(row_sizer);

    style_bubble(row);
    return row;
}

void OllamaChatMessageList::rewrap_row(Row& row)
{
    if (!row.label || !row.bubble)
        return;
    row.label->SetLabel(row.text);
    const int    wrap    = bubble_wrap_width();
    const wxSize text_sz = measure_wrapped_label(row.label, wrap, this);
    const int    label_w = std::min(text_sz.GetWidth(), wrap);
    const int    label_h = text_sz.GetHeight();
    row.label->SetMinSize(wxSize(label_w, label_h));
    row.label->SetMaxSize(wxSize(wrap, -1));
    const int pad_x  = FromDIP(kBubblePadXDp);
    const int pad_y  = FromDIP(bubble_pad_y_dp(row.kind));
    const int border = bubble_border_px(this);
    const int bubble_w = std::min(label_w + 2 * pad_x + 2 * border, max_bubble_width());
    const int bubble_h = label_h + 2 * pad_y + 2 * border;
    row.bubble->SetMinSize(wxSize(bubble_w, bubble_h));
    row.bubble->SetMaxSize(wxSize(max_bubble_width(), -1));
    row.row_panel->SetMinSize(wxSize(-1, bubble_h));
    row.bubble->Layout();
    row.row_panel->Layout();
}

void OllamaChatMessageList::destroy_row(Row& row)
{
    if (row.row_panel) {
        m_sizer->Detach(row.row_panel);
        row.row_panel->Destroy();
    }
    row = Row{};
}

size_t OllamaChatMessageList::insert_pos_before_pending() const
{
    size_t pos = m_sizer->GetItemCount();
    if (m_pending_row && pos > 0)
        --pos;
    return pos;
}

void OllamaChatMessageList::insert_row_before_pending(Row& row, int gap_dp, int align_flags)
{
    m_sizer->Insert(insert_pos_before_pending(), row.row_panel, 0, align_flags, FromDIP(gap_dp));
    rewrap_row(row);
}

void OllamaChatMessageList::maybe_rewrap_for_scrollbar()
{
    const bool needs_sb = GetVirtualSize().GetHeight() > GetClientSize().GetHeight();
    if (needs_sb == m_scrollbar_shown)
        return;
    m_scrollbar_shown = needs_sb;
    rewrap_all();
}

void OllamaChatMessageList::rewrap_all()
{
    for (Row& row : m_rows)
        rewrap_row(row);
    for (Row& row : m_thinking_rows)
        rewrap_row(row);
    if (m_stream_row)
        rewrap_row(*m_stream_row);
    if (m_pending_row)
        rewrap_row(*m_pending_row);
    layout_transcript(false);
}

void OllamaChatMessageList::layout_transcript(bool scroll_to_end)
{
    FitInside();
    Layout();
    maybe_rewrap_for_scrollbar();
    if (scroll_to_end)
        scroll_to_bottom();
}

void OllamaChatMessageList::on_size(wxSizeEvent& evt)
{
    const int width = GetClientSize().GetWidth();
    if (width > 0 && width != m_last_wrap_width) {
        m_last_wrap_width = width;
        CallAfter([this]() { rewrap_all(); });
    }
    evt.Skip();
}

void OllamaChatMessageList::scroll_to_bottom()
{
    FitInside();
    Layout();
    maybe_rewrap_for_scrollbar();
    const int unit_y = std::max(1, FromDIP(10));
    const int virt_y = GetVirtualSize().GetHeight();
    Scroll(0, virt_y / unit_y + 1);
    CallAfter([this]() {
        FitInside();
        Layout();
        maybe_rewrap_for_scrollbar();
        const int unit = std::max(1, FromDIP(10));
        Scroll(0, GetVirtualSize().GetHeight() / unit + 1);
    });
}

void OllamaChatMessageList::drop_oldest_if_needed()
{
    while (m_rows.size() > kMaxMessages) {
        const size_t drop_idx = (!m_rows.empty() && m_rows.front().role == ChatMessageRole::System
                                 && m_rows.size() > 1) ? 1 : 0;
        Row& victim = m_rows[drop_idx];
        destroy_row(victim);
        m_rows.erase(m_rows.begin() + static_cast<long>(drop_idx));
    }
}

int OllamaChatMessageList::append_message(ChatMessageRole role, const wxString& text, ChatMessageKind kind)
{
    Row row = make_row(role, kind, text, /*pending=*/false);

    int gap_dp = 10;
    if (m_rows.empty()) {
        gap_dp = 0;
    } else {
        const Row& prev          = m_rows.back();
        const bool prev_progress = prev.kind == ChatMessageKind::Progress;
        const bool cur_progress  = kind == ChatMessageKind::Progress;
        if (prev_progress && cur_progress)
            gap_dp = 4;
        else if (prev_progress || cur_progress)
            gap_dp = 6;
        else if (prev.role == role)
            gap_dp = 4;
    }

    size_t insert_pos = m_sizer->GetItemCount();
    if (m_pending_row && insert_pos > 0)
        --insert_pos;
    m_sizer->Insert(insert_pos, row.row_panel, 0, row_align_flags(role, kind), FromDIP(gap_dp));

    m_rows.push_back(row);
    rewrap_row(m_rows.back());
    drop_oldest_if_needed();
    layout_transcript(true);
    return row.id;
}

void OllamaChatMessageList::update_message(int id, const wxString& text)
{
    for (Row& row : m_rows) {
        if (row.id != id)
            continue;
        row.text = text;
        rewrap_row(row);
        layout_transcript(true);
        return;
    }
}

void OllamaChatMessageList::set_first_system_message(const wxString& text)
{
    for (Row& row : m_rows) {
        if (row.role != ChatMessageRole::System)
            continue;
        row.text = text;
        rewrap_row(row);
        layout_transcript(false);
        return;
    }
    Row row = make_row(ChatMessageRole::System, ChatMessageKind::Normal, text, /*pending=*/false);
    m_sizer->Insert(0, row.row_panel, 0, row_align_flags(row.role, row.kind), FromDIP(8));
    m_rows.insert(m_rows.begin(), row);
    rewrap_row(m_rows.front());
    layout_transcript(false);
}

wxString OllamaChatMessageList::last_assistant_text() const
{
    for (auto it = m_rows.rbegin(); it != m_rows.rend(); ++it) {
        if (it->role == ChatMessageRole::Assistant)
            return it->text;
    }
    return {};
}

void OllamaChatMessageList::clear_thinking_rows()
{
    for (Row& row : m_thinking_rows)
        destroy_row(row);
    m_thinking_rows.clear();
    if (m_stream_row) {
        destroy_row(m_stream_storage);
        m_stream_row = nullptr;
    }
}

void OllamaChatMessageList::clear()
{
    clear_pending();
    for (Row& row : m_rows)
        destroy_row(row);
    m_rows.clear();
    m_scrollbar_shown = false;
    layout_transcript(false);
    Scroll(0, 0);
}

// --- Pending bubble ------------------------------------------------------

void OllamaChatMessageList::begin_pending(const wxString& caption)
{
    if (m_pending_row) {
        m_pending_caption = caption;
        refresh_pending_caption(true);
        return;
    }
    clear_thinking_rows();
    m_pending_caption = caption;
    m_pending_dots    = 0;

    m_pending_storage = make_row(ChatMessageRole::Assistant, ChatMessageKind::Progress, wxEmptyString,
                                 /*pending=*/true);
    m_pending_row = &m_pending_storage;
    m_sizer->Add(m_pending_storage.row_panel, 0, wxALIGN_LEFT | wxTOP,
                 FromDIP(m_rows.empty() && m_thinking_rows.empty() ? 0 : 6));
    refresh_pending_caption(true);
    if (m_pending_timer)
        m_pending_timer->Start(kPendingAnimMs);
}

void OllamaChatMessageList::append_pending_line(const wxString& line)
{
    if (line.IsEmpty())
        return;
    if (!m_pending_row)
        begin_pending(m_pending_caption);

    Row row = make_row(ChatMessageRole::Assistant, ChatMessageKind::Progress, line, /*pending=*/false);
    m_thinking_rows.push_back(row);
    insert_row_before_pending(m_thinking_rows.back(), 4, wxALIGN_LEFT | wxTOP);
    layout_transcript(true);
}

void OllamaChatMessageList::set_pending_stream_text(const wxString& text)
{
    if (!m_pending_row)
        begin_pending(m_pending_caption);

    if (text.IsEmpty()) {
        if (m_stream_row) {
            destroy_row(m_stream_storage);
            m_stream_row = nullptr;
            layout_transcript(true);
        }
        return;
    }

    if (!m_stream_row) {
        m_stream_storage = make_row(ChatMessageRole::Assistant, ChatMessageKind::Progress, text, /*pending=*/false);
        m_stream_row     = &m_stream_storage;
        insert_row_before_pending(m_stream_storage, 4, wxALIGN_LEFT | wxTOP);
    } else {
        m_stream_storage.text = text;
        rewrap_row(m_stream_storage);
    }
    layout_transcript(true);
}

void OllamaChatMessageList::clear_pending()
{
    if (m_pending_timer)
        m_pending_timer->Stop();
    clear_thinking_rows();
    if (!m_pending_row)
        return;
    if (m_pending_storage.row_panel) {
        m_sizer->Detach(m_pending_storage.row_panel);
        m_pending_storage.row_panel->Destroy();
    }
    m_pending_storage = Row{};
    m_pending_row     = nullptr;
    layout_transcript(false);
}

void OllamaChatMessageList::on_pending_timer()
{
    if (!m_pending_row) {
        if (m_pending_timer)
            m_pending_timer->Stop();
        return;
    }
    m_pending_dots = (m_pending_dots + 1) % 4;
    refresh_pending_caption(true);
}

void OllamaChatMessageList::refresh_pending_caption(bool scroll_to_end)
{
    if (!m_pending_row)
        return;
    wxString dots;
    for (int i = 0; i < m_pending_dots; ++i)
        dots += wxT(" \u00B7");
    m_pending_row->text = m_pending_caption + dots;
    rewrap_row(*m_pending_row);
    layout_transcript(scroll_to_end);
}

}} // namespace Slic3r::GUI
