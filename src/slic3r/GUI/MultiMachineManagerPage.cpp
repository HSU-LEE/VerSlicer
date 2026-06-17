#include "MultiMachineManagerPage.hpp"
#include "BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Widgets/Label.hpp"

#include "DeviceCore/DevManager.h"

#include <algorithm>

namespace {
using Slic3r::GUI::SlicePilotUi::Theme;
}

namespace Slic3r {
namespace GUI {

DeviceRowColumnLayout device_row_layout(wxWindow* w, int row_width)
{
    DeviceRowColumnLayout c;
    if (!w || row_width <= 0)
        return c;

    c.pad_left  = w->FromDIP(DEVICE_LEFT_PADDING_LEFT);
    c.dev_w     = w->FromDIP(DEVICE_LEFT_DEV_NAME);
    c.task_w    = w->FromDIP(DEVICE_LEFT_PRO_NAME);
    c.action_w  = w->FromDIP(72);
    c.btn_h     = w->FromDIP(32);
    c.gap       = w->FromDIP(10);

    const int action_col = w->FromDIP(DEVICE_LEFT_ACTION);
    const int max_status = w->FromDIP(DEVICE_LEFT_PRO_INFO);
    const int min_status = w->FromDIP(120);

    c.status_x = c.pad_left + c.dev_w + c.task_w;
    c.action_x = row_width - c.pad_left - c.action_w;
    c.status_w = c.action_x - c.gap - c.status_x;

    if (c.status_w > max_status)
        c.status_w = max_status;
    if (c.status_w < min_status)
        c.status_w = std::max(0, c.status_w);
    if (c.status_x + c.status_w + c.gap > c.action_x)
        c.status_w = std::max(0, c.action_x - c.gap - c.status_x);

    return c;
}

MultiMachineItem::MultiMachineItem(wxWindow* parent, MachineObject* obj)
    : DeviceItem(parent, obj)
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(-1, FromDIP(DEVICE_ITEM_MAX_HEIGHT)));

    Bind(wxEVT_PAINT, &MultiMachineItem::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &MultiMachineItem::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &MultiMachineItem::OnLeaveWindow, this);
    Bind(wxEVT_LEFT_DOWN, &MultiMachineItem::OnLeftDown, this);
    Bind(wxEVT_MOTION, &MultiMachineItem::OnMove, this);
    Bind(EVT_MULTI_DEVICE_VIEW, [this, obj](auto& e) {
        wxGetApp().mainframe->jump_to_monitor(obj->get_dev_id());
        if (wxGetApp().mainframe->m_monitor->get_status_panel()->get_media_play_ctrl()) {
            wxGetApp().mainframe->m_monitor->get_status_panel()->get_media_play_ctrl()->jump_to_play();
        }
    });
    wxGetApp().UpdateDarkUIWin(this);
}

void MultiMachineItem::OnEnterWindow(wxMouseEvent& evt)
{
    m_hover = true;
    Refresh();
}

void MultiMachineItem::OnLeaveWindow(wxMouseEvent& evt)
{
    m_hover = false;
    Refresh();
}

void MultiMachineItem::OnLeftDown(wxMouseEvent& evt)
{
    const DeviceRowColumnLayout col = device_row_layout(this, GetSize().GetWidth());
    auto mouse_pos = ClientToScreen(evt.GetPosition());
    auto item = this->ClientToScreen(wxPoint(0, 0));

    if (mouse_pos.x >= (item.x + col.action_x) &&
        mouse_pos.x < (item.x + col.action_x + col.action_w) &&
        mouse_pos.y >= item.y &&
        mouse_pos.y < (item.y + FromDIP(DEVICE_ITEM_MAX_HEIGHT))) {
        post_event(wxCommandEvent(EVT_MULTI_DEVICE_VIEW));
    }
}

void MultiMachineItem::OnMove(wxMouseEvent& evt)
{
    const DeviceRowColumnLayout col = device_row_layout(this, GetSize().GetWidth());
    auto mouse_pos = ClientToScreen(evt.GetPosition());
    auto item = this->ClientToScreen(wxPoint(0, 0));

    if (mouse_pos.x >= (item.x + col.action_x) &&
        mouse_pos.x < (item.x + col.action_x + col.action_w) &&
        mouse_pos.y >= item.y &&
        mouse_pos.y < (item.y + FromDIP(DEVICE_ITEM_MAX_HEIGHT))) {
        SetCursor(wxCURSOR_HAND);
    }
    else {
        SetCursor(wxCURSOR_ARROW);
    }
}

void MultiMachineItem::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void MultiMachineItem::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void MultiMachineItem::DrawTextWithEllipsis(wxDC& dc, const wxString& text, int maxWidth, int left, int top) {
    wxSize size = GetSize();
    wxFont font = dc.GetFont();

    wxSize textSize = dc.GetTextExtent(text);
    dc.SetTextForeground(Theme::text());
    int textWidth = textSize.GetWidth();

    if (textWidth > maxWidth) {
        wxString truncatedText = text;
        int ellipsisWidth = dc.GetTextExtent("...").GetWidth();
        int numChars = text.length();

        for (int i = numChars - 1; i >= 0; --i) {
            truncatedText = text.substr(0, i) + "...";
            int truncatedWidth = dc.GetTextExtent(truncatedText).GetWidth();

            if (truncatedWidth <= maxWidth - ellipsisWidth) {
                break;
            }
        }

        if (top == 0) {
            dc.DrawText(truncatedText, left, (size.y - textSize.y) / 2);
        }
        else {
            dc.DrawText(truncatedText, left, (size.y - textSize.y) / 2 - top);
        }

    }
    else {
        if (top == 0) {
            dc.DrawText(text, left, (size.y - textSize.y) / 2);
        }
        else {
            dc.DrawText(text, left, (size.y - textSize.y) / 2 - top);
        }
    }
}

void MultiMachineItem::doRender(wxDC& dc)
{
    wxSize size = GetSize();
    const DeviceRowColumnLayout col = device_row_layout(this, size.x);
    const wxColour row_bg = (m_row_index % 2 == 0) ? Theme::surface() : Theme::surface_alt();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(row_bg));
    dc.DrawRectangle(0, 0, size.x, size.y);

    int left = col.pad_left;
    const int status_w = col.status_w;

    if (obj_) {
        //dev name
        wxString dev_name = wxString::FromUTF8(obj_->get_dev_name());
        if (!obj_->is_online()) {
            dev_name = dev_name + "(" + _L("Offline") + ")";
        }
        dc.SetFont(Label::Body_14);
        DrawTextWithEllipsis(dc, dev_name, col.dev_w, left);
        left += col.dev_w;

        //project name
        wxString project_name = _L("No task");
        if (obj_->is_in_printing()) {
            project_name = wxString::Format("%s", GUI::from_u8(obj_->subtask_name));
        }
        dc.SetFont(Label::Body_14);
        DrawTextWithEllipsis(dc, project_name, col.task_w, left);
        left = col.status_x;

        //state
        dc.SetFont(Label::Body_14);
        if (state_device == 0) {
            dc.SetTextForeground(*wxBLACK);
            DrawTextWithEllipsis(dc, get_state_device(), status_w, left);
        }
        else if (state_device == 1) {
            dc.SetTextForeground(wxColour(0,174,66));
            DrawTextWithEllipsis(dc, get_state_device(), status_w, left);
        }
        else if (state_device == 2)
        {
            dc.SetTextForeground(wxColour(208,27,27));
            DrawTextWithEllipsis(dc, get_state_device(), status_w, left);
        }
        else if (state_device > 2 && state_device < 7) {
            dc.SetFont(Label::Body_12);
            dc.SetTextForeground(wxColour(255, 143, 74));
            if (obj_->get_curr_stage() == _L("Printing") && obj_->subtask_) {
                wxString progress_info = wxString::Format("%d", obj_->subtask_->task_progress);
                wxString left_time = wxString::Format("%s", get_left_time(obj_->mc_left_time));

                DrawTextWithEllipsis(dc, progress_info + "%  |  " + left_time, status_w, left, FromDIP(10));

                const int bar_h = FromDIP(10);
                const int bar_y = FromDIP(30);
                const int bar_w = std::max(0, status_w - FromDIP(4));
                const int fill_w = static_cast<int>(bar_w * (static_cast<float>(obj_->subtask_->task_progress) / 100.0f));

                dc.SetPen(wxPen(wxColour(233,233,233)));
                dc.SetBrush(wxBrush(wxColour(233,233,233)));
                dc.DrawRoundedRectangle(left, bar_y, bar_w, bar_h, 2);

                dc.SetPen(wxPen(wxColour(255, 143, 74)));
                dc.SetBrush(wxBrush(wxColour(255, 143, 74)));
                if (fill_w > 0)
                    dc.DrawRoundedRectangle(left, bar_y, fill_w, bar_h, 2);
            }
            else {
                DrawTextWithEllipsis(dc, obj_->get_curr_stage(), status_w, left);
            }

        }
        else {
            dc.SetTextForeground(*wxBLACK);
            DrawTextWithEllipsis(dc, get_state_device(), status_w, left);
        }

        // View action
        const int btn_w = col.action_w;
        const int btn_h = col.btn_h;
        const int btn_x = col.action_x;
        const int btn_y = (size.y - btn_h) / 2;
        const wxColour btn_border = m_hover ? Theme::primary() : Theme::border();
        dc.SetPen(wxPen(btn_border));
        dc.SetBrush(wxBrush(m_hover ? Theme::primary_soft() : Theme::surface()));
        dc.DrawRoundedRectangle(btn_x, btn_y, btn_w, btn_h, FromDIP(6));
        dc.SetFont(Label::Body_13);
        dc.SetTextForeground(Theme::text());
        const wxString view_lbl = _L("View");
        dc.DrawText(view_lbl, btn_x + (btn_w - dc.GetTextExtent(view_lbl).x) / 2,
                    btn_y + (btn_h - dc.GetTextExtent(view_lbl).y) / 2);
    }

    if (m_hover) {
        dc.SetPen(wxPen(Theme::primary()));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(4));
    }
}

void MultiMachineItem::post_event(wxCommandEvent&& event)
{
    event.SetEventObject(this);
    event.SetString(obj_->get_dev_id());
    event.SetInt(state_selected);
    wxPostEvent(this, event);
}

void MultiMachineItem::DoSetSize(int x, int y, int width, int height, int sizeFlags /*= wxSIZE_AUTO*/)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
}

wxString MultiMachineItem::get_left_time(int mc_left_time)
{
    // update gcode progress
    std::string left_time;
    wxString    left_time_text = _L("N/A");

    try {
        left_time = get_bbl_monitor_time_dhm(mc_left_time);
    }
    catch (...) {
        ;
    }

    if (!left_time.empty()) left_time_text = wxString::Format("-%s", left_time);
    return left_time_text;
}


MultiMachineManagerPage::MultiMachineManagerPage(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    SlicePilotUi::apply_panel_chrome(this);
    m_main_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_main_panel->SetBackgroundColour(Theme::background());
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    const int side = SlicePilotUi::content_side_margin_dip(this, false);

    StateColor head_bg(
        std::pair<wxColour, int>(Theme::border(), StateColor::Pressed),
        std::pair<wxColour, int>(Theme::surface_alt(), StateColor::Normal)
    );

    m_main_sizer->Add(SlicePilotUi::create_header(m_main_panel, _L("Devices"),
        _L("Monitor and open printers added to multi-device management (up to 6)."), true),
        0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, side);

    auto* sizer_button_printer = new wxBoxSizer(wxHORIZONTAL);
    m_button_edit = new Button(m_main_panel, _L("Edit Printers"));
    m_button_edit->SetStyle(ButtonStyle::Confirm, ButtonType::Window);
    m_button_edit->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        MultiMachinePickPage dlg;
        dlg.ShowModal();
        refresh_user_device();
        evt.Skip();
    });
    sizer_button_printer->AddStretchSpacer(1);
    sizer_button_printer->Add(m_button_edit, 0, wxALIGN_CENTER_VERTICAL, 0);

    m_table_head_panel = new wxPanel(m_main_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_table_head_panel->SetBackgroundColour(Theme::surface_alt());
    m_table_head_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_printer_name = new Button(m_table_head_panel, _L("Device Name"), "toolbar_double_directional_arrow", wxNO_BORDER, ICON_SINGLE_SIZE);
    m_printer_name->SetBackgroundColor(head_bg);
    m_printer_name->SetFont(TABLE_HEAD_FONT);
    m_printer_name->SetCornerRadius(0);
    m_printer_name->SetMinSize(wxSize(FromDIP(DEVICE_LEFT_DEV_NAME), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_printer_name->SetMaxSize(wxSize(FromDIP(DEVICE_LEFT_DEV_NAME), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_printer_name->SetCenter(false);
    m_printer_name->Bind(wxEVT_ENTER_WINDOW, [&](wxMouseEvent& evt) {
        SetCursor(wxCURSOR_HAND);
        });
    m_printer_name->Bind(wxEVT_LEAVE_WINDOW, [&](wxMouseEvent& evt) {
        SetCursor(wxCURSOR_ARROW);
        });
    m_printer_name->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& evt) {
        device_dev_name_big = !device_dev_name_big;
        auto sortcb = [this](ObjState s1, ObjState s2) {
            return device_dev_name_big ? s1.state_dev_name > s2.state_dev_name : s1.state_dev_name < s2.state_dev_name;
        };
        this->m_sort.set_role(sortcb, SortItem::SR_MACHINE_NAME, device_dev_name_big);
        this->refresh_user_device();
    });


    m_task_name = new Button(m_table_head_panel, _L("Task Name"), "", wxNO_BORDER, ICON_SINGLE_SIZE);
    m_task_name->SetBackgroundColor(Theme::surface_alt());
    m_task_name->SetFont(TABLE_HEAD_FONT);
    m_task_name->SetCornerRadius(0);
    m_task_name->SetMinSize(wxSize(FromDIP(DEVICE_LEFT_PRO_NAME), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_task_name->SetMaxSize(wxSize(FromDIP(DEVICE_LEFT_PRO_NAME), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_task_name->SetCenter(false);



    m_status = new Button(m_table_head_panel, _L("Device Status"), "toolbar_double_directional_arrow", wxNO_BORDER, ICON_SINGLE_SIZE);
    m_status->SetBackgroundColor(head_bg);
    m_status->SetFont(TABLE_HEAD_FONT);
    m_status->SetCornerRadius(0);
    m_status->SetMinSize(wxSize(FromDIP(DEVICE_LEFT_PRO_INFO), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_status->SetMaxSize(wxSize(FromDIP(DEVICE_LEFT_PRO_INFO), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_status->SetCenter(false);
    m_status->Bind(wxEVT_ENTER_WINDOW, [&](wxMouseEvent& evt) {
        SetCursor(wxCURSOR_HAND);
        });
    m_status->Bind(wxEVT_LEAVE_WINDOW, [&](wxMouseEvent& evt) {
        SetCursor(wxCURSOR_ARROW);
        });
    m_status->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& evt) {
        device_state_big = !device_state_big;
        auto sortcb = [this](ObjState s1, ObjState s2) {
            return device_state_big ? s1.state_device > s2.state_device : s1.state_device < s2.state_device;
            };
        this->m_sort.set_role(sortcb, SortItem::SortRule::SR_MACHINE_STATE, device_state_big);
        this->refresh_user_device();
    });


    m_action = new Button(m_table_head_panel, _L("Actions"), "", wxNO_BORDER, ICON_SINGLE_SIZE, false);
    m_action->SetBackgroundColor(Theme::surface_alt());
    m_action->SetFont(TABLE_HEAD_FONT);
    m_action->SetCornerRadius(0);
    m_action->SetMinSize(wxSize(FromDIP(DEVICE_LEFT_ACTION), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_action->SetMaxSize(wxSize(FromDIP(DEVICE_LEFT_ACTION), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_action->SetCenter(false);


    m_table_head_sizer->AddSpacer(FromDIP(DEVICE_LEFT_PADDING_LEFT));
    m_table_head_sizer->Add(m_printer_name, 0, wxALIGN_CENTER_VERTICAL, 0);
    m_table_head_sizer->Add(m_task_name, 0, wxALIGN_CENTER_VERTICAL, 0);
    m_table_head_sizer->Add(m_status, 0, wxALIGN_CENTER_VERTICAL, 0);
    m_table_head_sizer->AddStretchSpacer(1);
    m_table_head_sizer->Add(m_action, 0, wxALIGN_CENTER_VERTICAL, 0);

    m_table_head_panel->SetSizer(m_table_head_sizer);
    m_table_head_panel->Layout();

    m_empty_panel = new wxPanel(m_main_panel, wxID_ANY);
    m_empty_panel->SetBackgroundColour(Theme::background());
    auto* empty_sizer = new wxBoxSizer(wxVERTICAL);
    empty_sizer->AddStretchSpacer(2);

    m_tip_text = new wxStaticText(m_empty_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    m_tip_text->SetLabel(_L("No printers selected. Add devices to manage them from this screen."));
    m_tip_text->SetForegroundColour(Theme::text_muted());
    m_tip_text->SetFont(::Label::Body_14);
    m_tip_text->Wrap(FromDIP(480));
    empty_sizer->Add(m_tip_text, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, side);

    m_button_add = new Button(m_empty_panel, _L("Add"));
    m_button_add->SetStyle(ButtonStyle::Confirm, ButtonType::Window);

    m_button_add->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
        MultiMachinePickPage dlg;
        dlg.ShowModal();
        refresh_user_device();
        evt.Skip();
    });
    empty_sizer->Add(m_button_add, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(12));
    empty_sizer->AddStretchSpacer(3);
    m_empty_panel->SetSizer(empty_sizer);

    m_machine_list = new wxScrolledWindow(m_main_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxVSCROLL | wxBORDER_NONE);
    m_machine_list->SetBackgroundColour(Theme::surface());
    m_machine_list->SetScrollRate(0, 5);
    m_machine_list->SetMinSize(wxSize(-1, FromDIP(DEVICE_ITEM_MAX_HEIGHT) * 4));

    m_sizer_machine_list = new wxBoxSizer(wxVERTICAL);
    m_machine_list->SetSizer(m_sizer_machine_list);
    m_machine_list->Layout();
    m_machine_list->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        evt.Skip();
        const int list_w = std::max(m_machine_list->GetClientSize().GetWidth(), FromDIP(720));
        sync_table_column_widths(list_w);
        for (MultiMachineItem* di : m_device_items) {
            if (di)
                di->SetMinSize(wxSize(list_w, FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
        }
        m_sizer_machine_list->Layout();
        for (MultiMachineItem* di : m_device_items) {
            if (di)
                di->Refresh();
        }
    });

    // add flipping page
    StateColor ctrl_bg(
        std::pair<wxColour, int>(CTRL_BUTTON_PRESSEN_COLOUR, StateColor::Pressed),
        std::pair<wxColour, int>(CTRL_BUTTON_NORMAL_COLOUR, StateColor::Normal)
    );

    m_flipping_panel = new wxPanel(m_main_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_flipping_panel->SetBackgroundColour(Theme::background());

    m_flipping_page_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_page_sizer = new wxBoxSizer(wxVERTICAL);
    btn_last_page = new Button(m_flipping_panel, "", "go_last_plate", 0, FromDIP(20));
    btn_last_page->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    btn_last_page->SetMaxSize(wxSize(FromDIP(20), FromDIP(20)));
    btn_last_page->SetBackgroundColor(head_bg);
    btn_last_page->Bind(wxEVT_LEFT_DOWN, [&](wxMouseEvent& evt) {
        evt.Skip();
        if (m_current_page == 0)
            return;
        btn_last_page->Enable(false);
        btn_next_page->Enable(false);
        start_timer();
        m_current_page--;
        if (m_current_page < 0)
            m_current_page = 0;
        refresh_user_device();
        update_page_number();
    });
    st_page_number = new wxStaticText(m_flipping_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
    btn_next_page = new Button(m_flipping_panel, "", "go_next_plate", 0, FromDIP(20));
    btn_next_page->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    btn_next_page->SetMaxSize(wxSize(FromDIP(20), FromDIP(20)));
    btn_next_page->SetBackgroundColor(head_bg);
    btn_next_page->Bind(wxEVT_LEFT_DOWN, [&](wxMouseEvent& evt) {
        evt.Skip();
        if (m_current_page == m_total_page - 1)
            return;
        btn_last_page->Enable(false);
        btn_next_page->Enable(false);
        start_timer();
        m_current_page++;
        if (m_current_page > m_total_page - 1)
            m_current_page = m_total_page - 1;
        refresh_user_device();
        update_page_number();
    });

    m_page_num_input = new ::TextInput(m_flipping_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(50), -1), wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    m_page_num_input->SetBackgroundColor(input_bg);
    m_page_num_input->GetTextCtrl()->SetValue("1");
    wxTextValidator validator(wxFILTER_DIGITS);
    m_page_num_input->GetTextCtrl()->SetValidator(validator);
    m_page_num_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent& e) {
        page_num_enter_evt();
    });

    m_page_num_enter = new Button(m_flipping_panel, _("Go"));
    m_page_num_enter->SetMinSize(wxSize(FromDIP(25), FromDIP(25)));
    m_page_num_enter->SetMaxSize(wxSize(FromDIP(25), FromDIP(25)));
    m_page_num_enter->SetBackgroundColor(ctrl_bg);
    m_page_num_enter->SetCornerRadius(FromDIP(5));
    m_page_num_enter->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [&](auto& evt) {
        page_num_enter_evt();
    });

    m_flipping_page_sizer->Add(0, 0, 1, wxEXPAND, 0);
    m_flipping_page_sizer->Add(btn_last_page, 0, wxALIGN_CENTER, 0);
    m_flipping_page_sizer->Add(st_page_number, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    m_flipping_page_sizer->Add(btn_next_page, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    m_flipping_page_sizer->Add(m_page_num_input, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(20));
    m_flipping_page_sizer->Add(m_page_num_enter, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    m_page_sizer->Add(m_flipping_page_sizer, 0, wxALIGN_CENTER_HORIZONTAL, FromDIP(5));
    m_flipping_panel->SetSizer(m_page_sizer);
    m_flipping_panel->Layout();

    m_main_sizer->Add(sizer_button_printer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, side);
    m_main_sizer->AddSpacer(FromDIP(8));
    m_main_sizer->Add(m_table_head_panel, 0, wxEXPAND | wxLEFT | wxRIGHT, side);
    m_main_sizer->Add(m_machine_list, 1, wxEXPAND | wxLEFT | wxRIGHT, side);
    m_main_sizer->Add(m_empty_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, side);
    m_empty_panel->Hide();
    m_main_sizer->Add(m_flipping_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, side);
    m_main_panel->SetSizer(m_main_sizer);
    m_main_panel->Layout();
    page_sizer = new wxBoxSizer(wxVERTICAL);
    page_sizer->Add(m_main_panel, 1, wxEXPAND);

    SetSizer(page_sizer);
    Layout();
    Fit();

    Bind(wxEVT_TIMER, &MultiMachineManagerPage::on_timer, this);
}

void MultiMachineManagerPage::update_page()
{
    for (int i = 0; i < m_device_items.size(); i++) {
        m_device_items[i]->sync_state();
        m_device_items[i]->Refresh();
    }
}

void MultiMachineManagerPage::refresh_user_device(bool clear)
{
    m_sizer_machine_list->Clear(true);
    m_device_items.clear();

    if(clear) return;

    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    auto all_machine = dev->get_my_cloud_machine_list();
    auto user_machine = std::map<std::string, MachineObject*>();

    //selected machine
    for (int i = 0; i < PICK_DEVICE_MAX; i++) {
        auto dev_id = wxGetApp().app_config->get("multi_devices", std::to_string(i));

        if (all_machine.count(dev_id) > 0) {
            user_machine[dev_id] = all_machine[dev_id];
        }
    }


    m_total_count = user_machine.size();

    m_state_objs.clear();
    for (auto it = user_machine.begin(); it != user_machine.end(); ++it) {
        sync_state(it->second);
    }

    //sort
    if (m_sort.rule != SortItem::SortRule::SR_None) {
        std::sort(m_state_objs.begin(), m_state_objs.end(), m_sort.get_machine_call_back());
    }

    double result = static_cast<double>(user_machine.size()) / m_count_page_item;
    m_total_page = std::ceil(result);

    std::vector<ObjState> sort_devices = extractRange(m_state_objs, m_current_page * m_count_page_item, (m_current_page + 1) * m_count_page_item - 1 );
    std::vector<std::string> subscribe_list;

    for (auto i = 0; i < sort_devices.size(); ++i) {
        auto dev_id = sort_devices[i].dev_id;

        auto machine = user_machine[dev_id];

        MultiMachineItem* di = new MultiMachineItem(m_machine_list, machine);
        di->set_row_index(static_cast<int>(i));
        m_device_items.push_back(di);
        m_sizer_machine_list->Add(m_device_items[i], 0, wxEXPAND);

        subscribe_list.push_back(dev_id);
    }

    dev->subscribe_device_list(subscribe_list);

    const bool no_devices = m_device_items.empty();
    if (m_empty_panel)
        m_empty_panel->Show(no_devices);
    m_machine_list->Show(!no_devices);

    update_page_number();
    m_flipping_panel->Show(!no_devices && m_total_page > 1);
    const int list_w = std::max(m_machine_list->GetClientSize().GetWidth(), FromDIP(720));
    sync_table_column_widths(list_w);
    for (MultiMachineItem* di : m_device_items) {
        if (!di)
            continue;
        di->SetMinSize(wxSize(list_w, FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    }

    m_sizer_machine_list->Layout();
    Layout();
}

void MultiMachineManagerPage::sync_table_column_widths(int list_width)
{
    const DeviceRowColumnLayout col = device_row_layout(m_machine_list, list_width);
    const int head_h = FromDIP(DEVICE_ITEM_MAX_HEIGHT);
    if (m_status) {
        m_status->SetMinSize(wxSize(col.status_w, head_h));
        m_status->SetMaxSize(wxSize(col.status_w, head_h));
    }
    if (m_table_head_panel)
        m_table_head_panel->Layout();
}

std::vector<ObjState> MultiMachineManagerPage::extractRange(const std::vector<ObjState>& source, int start, int end) {
    std::vector<ObjState> result;

    if (start < 0 || start > end || source.size() <= 0) {
        return result;
    }

    if ( end >= source.size() ) {
        end = source.size();
    }

    auto startIter = source.begin() + start;
    auto endIter = source.begin() + end;
    result.assign(startIter, endIter);
    return result;
}

void MultiMachineManagerPage::sync_state(MachineObject* obj_)
{
    ObjState state_obj;

    if (obj_) {
        state_obj.dev_id = obj_->get_dev_id();
        state_obj.state_dev_name = obj_->get_dev_name();

        if (obj_->print_status == "IDLE") {
            state_obj.state_device = 0;
        }
        else if (obj_->print_status == "FINISH") {
            state_obj.state_device = 1;
        }
        else if (obj_->print_status == "FAILED") {
            state_obj.state_device = 2;
        }
        else if (obj_->print_status == "RUNNING") {
            state_obj.state_device = 3;
        }
        else if (obj_->print_status == "PAUSE") {
            state_obj.state_device = 4;
        }
        else if (obj_->print_status == "PREPARE") {
            state_obj.state_device = 5;
        }
        else if (obj_->print_status == "SLICING") {
            state_obj.state_device = 6;
        }
        else {
            state_obj.state_device = 7;
        }
    }
    m_state_objs.push_back(state_obj);
}

bool MultiMachineManagerPage::Show(bool show)
{
    if (show) {
        refresh_user_device();
    }
    else {
        Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (dev) {
            dev->subscribe_device_list(std::vector<std::string>());
        }
    }
    return wxPanel::Show(show);
}

void MultiMachineManagerPage::start_timer()
{
    if (m_flipping_timer) {
        m_flipping_timer->Stop();
    }
    else {
        m_flipping_timer = new wxTimer();
    }

    m_flipping_timer->SetOwner(this);
    m_flipping_timer->Start(1000);
    wxPostEvent(this, wxTimerEvent(*m_flipping_timer));
}

void MultiMachineManagerPage::update_page_number()
{
    double result = static_cast<double>(m_total_count) / m_count_page_item;
    m_total_page = std::ceil(result);

    wxString number = wxString(std::to_string(m_current_page + 1)) + " / " + wxString(std::to_string(m_total_page));
    st_page_number->SetLabel(number);
}

void MultiMachineManagerPage::on_timer(wxTimerEvent& event)
{
    m_flipping_timer->Stop();
    if (btn_last_page)
        btn_last_page->Enable(true);
    if (btn_next_page)
        btn_next_page->Enable(true);
}

void MultiMachineManagerPage::clear_page()
{

}

void MultiMachineManagerPage::page_num_enter_evt()
{
    btn_last_page->Enable(false);
    btn_next_page->Enable(false);
    start_timer();
    auto value = m_page_num_input->GetTextCtrl()->GetValue();
    long page_num = 0;
    if (value.ToLong(&page_num)) {
        if (page_num > m_total_page)
            m_current_page = m_total_page - 1;
        else if (page_num < 1)
            m_current_page = 0;
        else
            m_current_page = page_num - 1;
    }
    refresh_user_device();
    update_page_number();
}

void MultiMachineManagerPage::msw_rescale()
{
    m_printer_name->Rescale();
    m_printer_name->SetMinSize(wxSize(FromDIP(DEVICE_LEFT_DEV_NAME), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_printer_name->SetMaxSize(wxSize(FromDIP(DEVICE_LEFT_DEV_NAME), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_task_name->Rescale();
    m_task_name->SetMinSize(wxSize(FromDIP(DEVICE_LEFT_PRO_NAME), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_task_name->SetMaxSize(wxSize(FromDIP(DEVICE_LEFT_PRO_NAME), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_status->Rescale();
    m_action->Rescale();
    m_action->SetMinSize(wxSize(FromDIP(DEVICE_LEFT_ACTION), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_action->SetMaxSize(wxSize(FromDIP(DEVICE_LEFT_ACTION), FromDIP(DEVICE_ITEM_MAX_HEIGHT)));
    m_button_add->Rescale();
    m_button_add->SetMinSize(wxSize(FromDIP(90), FromDIP(36)));
    m_button_add->SetMaxSize(wxSize(FromDIP(90), FromDIP(36)));

    btn_last_page->Rescale();
    btn_last_page->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    btn_last_page->SetMaxSize(wxSize(FromDIP(20), FromDIP(20)));
    btn_next_page->Rescale();
    btn_next_page->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    btn_next_page->SetMaxSize(wxSize(FromDIP(20), FromDIP(20)));
    m_page_num_enter->Rescale();
    m_page_num_enter->SetMinSize(wxSize(FromDIP(25), FromDIP(25)));
    m_page_num_enter->SetMaxSize(wxSize(FromDIP(25), FromDIP(25)));

    m_button_edit->Rescale();
    m_button_edit->SetMinSize(wxSize(FromDIP(90), FromDIP(36)));
    m_button_edit->SetMaxSize(wxSize(FromDIP(90), FromDIP(36)));

    if (m_machine_list) {
        const int list_w = std::max(m_machine_list->GetClientSize().GetWidth(), FromDIP(720));
        sync_table_column_widths(list_w);
    }

    for (const auto& item : m_device_items) {
        item->Refresh();
    }

    Fit();
    Layout();
    Refresh();
}

} // namespace GUI
} // namespace Slic3r
