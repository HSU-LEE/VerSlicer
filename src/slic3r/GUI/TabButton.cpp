#include "TabButton.hpp"
#include "Widgets/Label.hpp"

#include <wx/dcclient.h>
#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(TabButton, StaticBox)

EVT_LEFT_DOWN(TabButton::mouseDown)
EVT_LEFT_UP(TabButton::mouseReleased)

// catch paint events
EVT_PAINT(TabButton::paintEvent)

END_EVENT_TABLE()

static wxColour BORDER_HOVER_COL = wxColour(255, 143, 74);

const static wxColour TAB_BUTTON_BG    = StateColor::darkModeColorFor(wxColour("#FEFFFF"));
const static wxColour TAB_BUTTON_SEL   = StateColor::darkModeColorFor(wxColour("#FFE0CC")); // ORCA

TabButton::TabButton()
    : paddingSize(18, 16) // ORCA reduce / match left margin buttons on sidebars
    , text_color(*wxBLACK)
{
    background_color = StateColor(
        std::make_pair(TAB_BUTTON_SEL, (int) StateColor::Checked),
        std::make_pair(StateColor::darkModeColorFor(wxColour("#FEFFFF")), (int) StateColor::Hovered),
        std::make_pair(StateColor::darkModeColorFor(wxColour("#FEFFFF")), (int) StateColor::Normal));

    border_color = StateColor(
        std::make_pair(TAB_BUTTON_SEL, (int) StateColor::Checked), // ORCA use same color for border to prevent 1px blank border
        std::make_pair(BORDER_HOVER_COL, (int) StateColor::Hovered),
        std::make_pair(StateColor::darkModeColorFor(wxColour("#FEFFFF")), (int)StateColor::Normal));
}

TabButton::TabButton(wxWindow *parent, wxString text, ScalableBitmap &bmp, long style, int iconSize)
    : TabButton()
{
    Create(parent, text, bmp, style, iconSize);
}

bool TabButton::Create(wxWindow *parent, wxString text, ScalableBitmap &bmp, long style, int iconSize)
{
    StaticBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);
    newtag_img = ScalableBitmap(this, "monitor_hms_new",7);
    state_handler.attach({&text_color, &border_color});
    state_handler.update_binds();
    //BBS set default font
    SetFont(Label::Body_14);
    wxWindow::SetLabel(text);
    this->icon = bmp;
    update_icon_slot_width();
    messureSize();
    return true;
}

void TabButton::SetLabel(const wxString &label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

void TabButton::SetMinSize(const wxSize &size)
{
    minSize = size;
    messureSize();
}

void TabButton::SetPaddingSize(const wxSize &size)
{
    paddingSize = size;
    messureSize();
}

const wxSize& TabButton::GetPaddingSize() 
{
    return paddingSize;
}

void TabButton::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void TabButton::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void TabButton::SetBGColor(StateColor const &color)
{
    background_color = color;
    state_handler.update_binds();
    Refresh();
}

void TabButton::SetBitmap(ScalableBitmap &bitmap)
{
    this->icon = bitmap;
    update_icon_slot_width();
}

void TabButton::update_icon_slot_width()
{
    m_right_icon_slot_w = FromDIP(14);
    if (newtag_img.bmp().IsOk())
        m_right_icon_slot_w = std::max(m_right_icon_slot_w, newtag_img.GetBmpSize().x);
}

bool TabButton::Enable(bool enable)
{
    bool result = wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
    }
    return result;
}

void TabButton::Rescale()
{
    update_icon_slot_width();
    messureSize();
}

void TabButton::paintEvent(wxPaintEvent &evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void TabButton::render(wxDC &dc)
{
    StaticBox::render(dc);
    int    states = state_handler.states();
    wxSize size   = GetSize();

    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(0, 0, size.x, size.y);

    // Right-side indicator: only show NEW tag (remove chevron arrows).
    wxBitmap right_img;
    int      right_extra = 0;
    if (show_new_tag) {
        right_img   = newtag_img.bmp();
        right_extra = FromDIP(4);
    }

    const int left_pad  = FromDIP(paddingSize.x);
    const int right_pad = FromDIP(paddingSize.x);

    const int icon_slot_w = right_img.IsOk() ? right_img.GetWidth() : 0;
    const int right_reserved = right_pad + (right_img.IsOk() ? (icon_slot_w + right_extra) : 0);
    const int avail_w = std::max(0, size.x - left_pad - right_reserved);
    const int row_center_y = size.y / 2;

    wxPoint pt{0, 0};

    auto text = GetLabel();
    if (!text.IsEmpty()) {
        // Left-align the label so it lines up with the arrow across rows.
        dc.SetFont(GetFont());
        if (left_pad + textSize.x > left_pad + avail_w)
            text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, avail_w);
        const wxSize text_extent = dc.GetTextExtent(text);
        pt.x = left_pad;
        pt.y = row_center_y - text_extent.y / 2;
        dc.SetTextForeground(text_color.colorForStates(states));
        dc.DrawText(text, pt);
    }

    if (right_img.IsOk()) {
        const int right_edge = size.x - right_pad - right_extra;
        pt.x = right_edge - icon_slot_w;
        pt.y = row_center_y - right_img.GetHeight() / 2;
        dc.DrawBitmap(right_img, pt);
    }

}

void TabButton::messureSize()
{
    wxClientDC dc(this);
    textSize = dc.GetTextExtent(GetLabel());
    if (minSize.GetWidth() > 0) {
        wxWindow::SetMinSize(minSize);
        return;
    }
    wxSize szContent = textSize;
    if (szContent.y > 0)
        szContent.x += FromDIP(5);
    // Reserve right-side space only for NEW tag; chevrons are removed.
    if (show_new_tag && m_right_icon_slot_w > 0)
        szContent.x += m_right_icon_slot_w;
    wxWindow::SetMinSize(szContent + paddingSize);
}

void TabButton::mouseDown(wxMouseEvent &event)
{
    event.Skip();
    pressedDown = true;
    SetFocus();
    CaptureMouse();
}

void TabButton::mouseReleased(wxMouseEvent &event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        ReleaseMouse();
        if (wxRect({0, 0}, GetSize()).Contains(event.GetPosition()))
            sendButtonEvent();
    }
}

void TabButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}
