#include "SlicePilotSimpleLayout.hpp"

#include "../GUI_App.hpp"
#include "../MainFrame.hpp"
#include "../Notebook.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r { namespace GUI {

namespace {

void set_page_visible(MainFrame* frame, wxWindow* page, bool visible)
{
    if (!frame || !frame->m_tabpanel || !page)
        return;
    const int idx = frame->m_tabpanel->FindPage(page);
    if (idx >= 0)
        frame->m_tabpanel->SetPageVisible(static_cast<size_t>(idx), visible);
}

void set_plater_tab_visible(MainFrame* frame, MainFrame::TabPosition tab, bool visible)
{
    if (!frame || !frame->m_tabpanel)
        return;
    const int idx = frame->page_index_for(tab);
    if (idx >= 0)
        frame->m_tabpanel->SetPageVisible(static_cast<size_t>(idx), visible);
}

} // namespace

void SlicePilotSimpleLayout::initialize_defaults(Slic3r::AppConfig* cfg)
{
    if (!cfg)
        return;
    if (!cfg->has(kConfigKey))
        cfg->set_bool(kConfigKey, true);
}

bool SlicePilotSimpleLayout::is_enabled()
{
    if (!wxGetApp().app_config)
        return false;
    return wxGetApp().app_config->get_bool(kConfigKey);
}

void SlicePilotSimpleLayout::set_enabled(bool enabled)
{
    if (!wxGetApp().app_config)
        return;
    wxGetApp().app_config->set_bool(kConfigKey, enabled);
    wxGetApp().app_config->save();
    apply(wxGetApp().mainframe);
}

void SlicePilotSimpleLayout::apply_orca_layout()
{
    set_enabled(false);
}

void SlicePilotSimpleLayout::apply(MainFrame* frame)
{
    if (!frame || !frame->m_tabpanel || !wxGetApp().plater())
        return;

    const bool simple = is_enabled();

    if (frame->IsBeingDeleted())
        return;

    if (simple) {
        set_page_visible(frame, frame->m_webview, false);
        set_page_visible(frame, frame->m_calibration, false);
        set_page_visible(frame, frame->m_smart_print_page, false);
        if (frame->m_multi_machine)
            set_page_visible(frame, frame->m_multi_machine, false);
        set_plater_tab_visible(frame, MainFrame::tp3DEditor, true);
        set_plater_tab_visible(frame, MainFrame::tpPreview, true);
        set_page_visible(frame, frame->m_monitor, true);
    } else {
        for (size_t i = 0; i < frame->m_tabpanel->GetPageCount(); ++i)
            frame->m_tabpanel->SetPageVisible(i, true);
        // Orca-like: Smart Print lives in Prepare strip, not a top-level tab.
        if (frame->m_smart_print_page)
            set_page_visible(frame, frame->m_smart_print_page, false);
        if (!wxGetApp().is_enable_multi_machine() && frame->m_multi_machine)
            set_page_visible(frame, frame->m_multi_machine, false);
    }

    frame->m_tabpanel->Refresh();
    frame->Layout();

    wxGetApp().CallAfter([simple]() {
        if (!wxGetApp().mainframe || !wxGetApp().plater())
            return;
        wxGetApp().save_mode(simple ? comSimple : comAdvanced);
    });
}

}} // namespace Slic3r::GUI
