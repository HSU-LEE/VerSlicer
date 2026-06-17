#include "SlicePilotNetworkSetup.hpp"
#include "PrintReadinessGate.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/msgdlg.h>

namespace Slic3r { namespace GUI {

void SlicePilotNetworkSetup::maybe_offer_first_run_plugin_install()
{
    if (!wxGetApp().app_config)
        return;
    static constexpr const char* kOffered = "slicepilot_network_plugin_offer_done";
    if (wxGetApp().app_config->get_bool(kOffered))
        return;
    if (PrintReadinessGate::network_plugin_ready()) {
        wxGetApp().app_config->set_bool(kOffered, true);
        wxGetApp().app_config->save();
        return;
    }

    wxGetApp().app_config->set_bool(kOffered, true);
    wxGetApp().app_config->save();

    wxWindow* parent = wxGetApp().plater() ? static_cast<wxWindow*>(wxGetApp().plater()) : nullptr;
    wxMessageDialog dlg(parent,
        _L("Verslicer works best with the Bambu Network plug-in for sending prints.\n\n"
           "Install it now? You can still slice and export G-code without it."),
        _L("Bambu Network plug-in"), wxYES_NO | wxICON_INFORMATION);
    dlg.SetYesNoLabels(_L("Install now"), _L("Later"));
    if (dlg.ShowModal() == wxID_YES)
        wxGetApp().ShowDownNetPluginDlg();
}

}} // namespace Slic3r::GUI
