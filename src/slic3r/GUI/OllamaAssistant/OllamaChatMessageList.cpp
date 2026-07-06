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

// Keep the transcript bounded; the oldest non-welcome bubble is dropped first.
constexpr size_t kMaxMessages = 200;
constexpr int    kPendingAnimMs = 400;

// Bubble inner padding (dp). These are the single source of truth for both the
// sizer borders in make_row AND the bubble min-size math in rewrap_row — they
// MUST stay in sync or trailing glyphs get clipped.
constexpr int kBubblePadXDp = 12;
constexpr int kBubblePadYDp = 8;
// Progress notes read as compact status lines, not conversation turns.
constexpr int kProgressPadYDp = 4;
// Breathing room around the whole transcript, so bubbles never sit flush
// against the panel edges and the last bubble keeps a margin at the bottom.
constexpr int kListPadDp = 8;

int bubble_pad_y_dp(ChatMessageKind kind)
{
    return kind == ChatMessageKind::Progress ? kProgressPadYDp : kBubblePadYDp;
}

int bubble_border_px(wxWindow* ref)
{
    return std::max(1, ref->FromDIP(1));
}

// wxStaticText::GetBestSize() after Wrap() can under-report CJK / multi-line
// extent on macOS; cross-check with a DC measurement and keep the larger value.
wxSize measure_wrapped_label(wxStaticText* label, int wrap_px, wxWindow* ref)
{
    label->Wrap(wrap_px);
    const wxSize best = label->GetBestSize();
    wxClientDC     dc(label);
    dc.SetFont(label->GetFont());
    const wxSize dc_sz = dc.GetMultiLineTextExtent(label->GetLabel());
    const int slack = ref->FromDIP(2);
    return wxSize(std::max(best.GetWidth(), dc_sz.GetWidth()) + slack,
                  std::max(best.GetHeight(), dc_sz.GetHeight()) + slack);
}

} // namespace

OllamaChatMessageList::OllamaChatMessageList(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE)
{
    SetBackgroundColour(SlicePilotUi::Theme::background());
    SetScrollRate(0, FromDIP(10));
    m_sizer = new wxBoxSizer(wxVERTICAL);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(m_sizer, 1, wxEXPAND | wxALL, FromDIP(kListPadDp));
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

int OllamaChatMessageList::bubble_wrap_width() const
{
    // Bubbles use at most ~70% of the viewport so long lines wrap and the
    // left/right alignment reads as a conversation. This is the width the
    // *label text* may occupy; the bubble adds kBubblePadXDp on each side, so
    // subtracting the full horizontal padding keeps the bubble itself within
    // 70% and leaves the remaining ~30% as margin even on narrow windows.
    const int client = GetClientSize().GetWidth();
    if (client < FromDIP(120))
        return FromDIP(320); // not laid out yet; rewrapped on first real size
    const int content = client - 2 * FromDIP(kListPadDp); // inside the outer inset
    return std::max(FromDIP(160), content * 70 / 100 - FromDIP(2 * kBubblePadXDp));
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
        // Accent-tinted filled bubble, right-aligned (alignment set in make_row).
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
        // Subtle surface fill (defaults above).
        break;
    }
    switch (row.kind) {
    case ChatMessageKind::Progress:
        // Pipeline/system notes: smaller, muted, borderless — clearly not
        // conversation (also centered; see make_row).
        bg     = Theme::background();
        border = Theme::background();
        fg     = Theme::text_muted();
        font   = Label::Body_12;
        break;
    case ChatMessageKind::Question:
        // Clarifying question awaiting a reply: accent border so it stands out.
        bg     = Theme::surface();
        border = Theme::primary();
        break;
    case ChatMessageKind::Error:
        // Failure: warning-tinted border, body text stays normal so long
        // error messages remain readable (red-on-orange read harsh).
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
    // Same FromDIP calls as rewrap_row so padding and min-size math can never
    // drift apart (that drift is exactly what clips trailing glyphs).
    const int pad_x = FromDIP(kBubblePadXDp);
    const int pad_y = FromDIP(bubble_pad_y_dp(kind));
    row.label = new wxStaticText(row.bubble, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxST_NO_AUTORESIZE);
    auto* bubble_col = new wxBoxSizer(wxVERTICAL);
    bubble_col->AddSpacer(pad_y);
    bubble_col->Add(row.label, 1, wxEXPAND | wxLEFT | wxRIGHT, pad_x);
    bubble_col->AddSpacer(pad_y);
    row.bubble->SetSizer(bubble_col);

    if (kind == ChatMessageKind::Progress && !pending) {
        // Progress/system notes are centered so they read as pipeline status,
        // not as a turn in the conversation.
        row_sizer->AddStretchSpacer(1);
        row_sizer->Add(row.bubble, 0, wxALIGN_CENTER_VERTICAL);
        row_sizer->AddStretchSpacer(1);
    } else if (role == ChatMessageRole::User) {
        row_sizer->AddStretchSpacer(1);
        row_sizer->Add(row.bubble, 0, wxALIGN_CENTER_VERTICAL);
    } else {
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
    const wxSize text_sz = measure_wrapped_label(row.label, bubble_wrap_width(), this);
    row.label->SetMinSize(text_sz);
    const int pad_x    = FromDIP(kBubblePadXDp);
    const int pad_y    = FromDIP(bubble_pad_y_dp(row.kind));
    const int border   = bubble_border_px(this);
    // StaticBox draws its border inside the widget bounds (over the outermost
    // pixels of the label), so reserve border width on every edge.
    row.bubble->SetMinSize(wxSize(text_sz.GetWidth() + 2 * pad_x + 2 * border,
                                  text_sz.GetHeight() + 2 * pad_y + 2 * border));
    row.bubble->Layout();
    row.row_panel->Layout();
}

void OllamaChatMessageList::rewrap_all()
{
    for (Row& row : m_rows)
        rewrap_row(row);
    if (m_pending_row)
        rewrap_row(*m_pending_row);
    FitInside();
    Layout();
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
    const int unit_y = std::max(1, FromDIP(10));
    const int virt_y = GetVirtualSize().GetHeight();
    Scroll(0, virt_y / unit_y + 1);
    // Re-scroll after the pending layout pass settles: a freshly-added row can
    // grow the virtual size after this call, leaving the view one bubble short.
    CallAfter([this]() {
        const int unit = std::max(1, FromDIP(10));
        Scroll(0, GetVirtualSize().GetHeight() / unit + 1);
    });
}

void OllamaChatMessageList::drop_oldest_if_needed()
{
    while (m_rows.size() > kMaxMessages) {
        // Preserve the leading system welcome bubble when present.
        const size_t drop_idx = (!m_rows.empty() && m_rows.front().role == ChatMessageRole::System
                                 && m_rows.size() > 1) ? 1 : 0;
        Row& victim = m_rows[drop_idx];
        if (victim.row_panel) {
            m_sizer->Detach(victim.row_panel);
            victim.row_panel->Destroy();
        }
        m_rows.erase(m_rows.begin() + static_cast<long>(drop_idx));
    }
}

int OllamaChatMessageList::append_message(ChatMessageRole role, const wxString& text, ChatMessageKind kind)
{
    Row row = make_row(role, kind, text, /*pending=*/false);

    // Spacing rhythm (gap ABOVE the new row, so gaps never double up):
    // 10dp between conversation turns, 4dp when the same speaker continues,
    // and a compact 4-6dp around progress notes so they read as status lines.
    int gap_dp = 10;
    if (m_rows.empty()) {
        gap_dp = 0;
    } else {
        const Row& prev          = m_rows.back();
        const bool prev_progress = prev.kind == ChatMessageKind::Progress;
        const bool cur_progress  = kind == ChatMessageKind::Progress;
        if (prev_progress && cur_progress)
            gap_dp = 4; // stacked status lines: one tight gap, never 2x
        else if (prev_progress || cur_progress)
            gap_dp = 6; // status line adjacent to a conversation bubble
        else if (prev.role == role)
            gap_dp = 4; // same speaker continues
    }

    // The pending bubble always stays last.
    size_t insert_pos = m_sizer->GetItemCount();
    if (m_pending_row && insert_pos > 0)
        --insert_pos;
    m_sizer->Insert(insert_pos, row.row_panel, 0, wxEXPAND | wxTOP, FromDIP(gap_dp));

    m_rows.push_back(row);
    rewrap_row(m_rows.back());
    drop_oldest_if_needed();
    scroll_to_bottom();
    return row.id;
}

void OllamaChatMessageList::update_message(int id, const wxString& text)
{
    for (Row& row : m_rows) {
        if (row.id != id)
            continue;
        row.text = text;
        rewrap_row(row);
        scroll_to_bottom();
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
        FitInside();
        Layout();
        return;
    }
    // No system bubble yet — create one at the top.
    Row row = make_row(ChatMessageRole::System, ChatMessageKind::Normal, text, /*pending=*/false);
    m_sizer->Insert(0, row.row_panel, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    m_rows.insert(m_rows.begin(), row);
    rewrap_row(m_rows.front());
    FitInside();
    Layout();
}

wxString OllamaChatMessageList::last_assistant_text() const
{
    for (auto it = m_rows.rbegin(); it != m_rows.rend(); ++it) {
        if (it->role == ChatMessageRole::Assistant)
            return it->text;
    }
    return {};
}

void OllamaChatMessageList::clear()
{
    clear_pending();
    for (Row& row : m_rows) {
        if (row.row_panel) {
            m_sizer->Detach(row.row_panel);
            row.row_panel->Destroy();
        }
    }
    m_rows.clear();
    FitInside();
    Layout();
    Scroll(0, 0);
}

// --- Pending bubble ------------------------------------------------------

void OllamaChatMessageList::begin_pending(const wxString& caption)
{
    if (m_pending_row) {
        m_pending_caption = caption;
        refresh_pending_label();
        return;
    }
    m_pending_caption = caption;
    m_pending_lines.Clear();
    m_pending_stream.Clear();
    m_pending_dots = 0;

    m_pending_storage = make_row(ChatMessageRole::Assistant, ChatMessageKind::Progress, wxEmptyString,
                                 /*pending=*/true);
    m_pending_row     = &m_pending_storage;
    // Pending is Progress-kind: use the same compact status-line gap.
    m_sizer->Add(m_pending_storage.row_panel, 0, wxEXPAND | wxTOP,
                 FromDIP(m_rows.empty() ? 0 : 6));
    refresh_pending_label();
    if (m_pending_timer)
        m_pending_timer->Start(kPendingAnimMs);
    scroll_to_bottom();
}

void OllamaChatMessageList::append_pending_line(const wxString& line)
{
    if (line.IsEmpty())
        return;
    if (!m_pending_row)
        begin_pending(m_pending_caption);
    if (!m_pending_lines.IsEmpty() && !m_pending_lines.EndsWith("\n"))
        m_pending_lines += "\n";
    m_pending_lines += line;
    refresh_pending_label();
    scroll_to_bottom();
}

void OllamaChatMessageList::set_pending_stream_text(const wxString& text)
{
    if (!m_pending_row)
        begin_pending(m_pending_caption);
    m_pending_stream = text;
    refresh_pending_label();
    scroll_to_bottom();
}

void OllamaChatMessageList::clear_pending()
{
    if (m_pending_timer)
        m_pending_timer->Stop();
    if (!m_pending_row)
        return;
    if (m_pending_storage.row_panel) {
        m_sizer->Detach(m_pending_storage.row_panel);
        m_pending_storage.row_panel->Destroy();
    }
    m_pending_storage = Row{};
    m_pending_row     = nullptr;
    m_pending_lines.Clear();
    m_pending_stream.Clear();
    FitInside();
    Layout();
}

void OllamaChatMessageList::on_pending_timer()
{
    if (!m_pending_row) {
        if (m_pending_timer)
            m_pending_timer->Stop();
        return;
    }
    m_pending_dots = (m_pending_dots + 1) % 4;
    refresh_pending_label();
}

void OllamaChatMessageList::refresh_pending_label()
{
    if (!m_pending_row)
        return;
    wxString dots;
    for (int i = 0; i < m_pending_dots; ++i)
        dots += wxT(" \u00B7");
    wxString text = m_pending_caption + dots;
    if (!m_pending_lines.IsEmpty())
        text += "\n" + m_pending_lines;
    if (!m_pending_stream.IsEmpty())
        text += "\n" + m_pending_stream;
    m_pending_row->text = text;
    rewrap_row(*m_pending_row);
    FitInside();
    Layout();
}

}} // namespace Slic3r::GUI
