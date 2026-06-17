#include "SlicePilotSimpleLayout.hpp"

#include "../GUI_App.hpp"
#include "../MainFrame.hpp"
#include "../Notebook.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r { namespace GUI {

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

void SlicePilotSimpleLayout::apply(MainFrame* frame)
{
    if (!frame || !frame->m_tabpanel || !wxGetApp().plater())
        return;

    const bool simple = is_enabled();

    // Tab visibility only until the main window layout is fully up (save_mode runs update_mode).
    if (frame->IsBeingDeleted())
        return;

    auto set_vis = [&](MainFrame::TabPosition tab, bool visible) {
        const int idx = static_cast<int>(tab);
        if (idx >= 0 && static_cast<size_t>(idx) < frame->m_tabpanel->GetPageCount())
            frame->m_tabpanel->SetPageVisible(static_cast<size_t>(idx), visible);
    };

    if (simple) {
        set_vis(MainFrame::tpHome, false);
        set_vis(MainFrame::tpCalibration, false);
        set_vis(MainFrame::tpAuxiliary, false);
        set_vis(MainFrame::toDebugTool, false);
        set_vis(MainFrame::tp3DEditor, true);
        set_vis(MainFrame::tpPreview, true);
        set_vis(MainFrame::tpMonitor, true);
        set_vis(MainFrame::tpSmartPrint, true);
        if (wxGetApp().is_enable_multi_machine())
            set_vis(MainFrame::tpMultiDevice, false);
    } else {
        for (size_t i = 0; i < frame->m_tabpanel->GetPageCount(); ++i)
            frame->m_tabpanel->SetPageVisible(i, true);
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
