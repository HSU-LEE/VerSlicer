#include "SlicePilotOnboardingFunnel.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "BambuSmartPrintService.hpp"
#include "PrintReadinessGate.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r { namespace GUI {

namespace {

static constexpr const char* kSection = "onboarding_funnel";

static AppConfig* cfg() { return wxGetApp().app_config; }

static bool get_flag(const char* key)
{
    return cfg() && cfg()->has(kSection, key) && cfg()->get(kSection, key) == "1";
}

static void set_flag(const char* key)
{
    if (!cfg())
        return;
    if (get_flag(key))
        return;
    cfg()->set(kSection, key, "1");
    cfg()->save();
}

} // namespace

void SlicePilotOnboardingFunnel::initialize_defaults(AppConfig* app_cfg)
{
    if (!app_cfg)
        return;
    if (!app_cfg->has_section(kSection))
        app_cfg->set(kSection, "wizard_completed", "0");
}

void SlicePilotOnboardingFunnel::record_wizard_completed() { set_flag("wizard_completed"); }
void SlicePilotOnboardingFunnel::record_plugin_installed() { set_flag("plugin_installed"); }
void SlicePilotOnboardingFunnel::record_printer_bound() { set_flag("printer_bound"); }
void SlicePilotOnboardingFunnel::record_first_slice() { set_flag("first_slice"); }
void SlicePilotOnboardingFunnel::record_first_send() { set_flag("first_send"); }
void SlicePilotOnboardingFunnel::record_first_failure_seen() { set_flag("first_failure_seen"); }
void SlicePilotOnboardingFunnel::record_first_reprint() { set_flag("first_reprint"); }
void SlicePilotOnboardingFunnel::record_smart_analysis_done() { set_flag("smart_analysis_done"); }

bool SlicePilotOnboardingFunnel::wizard_completed() { return get_flag("wizard_completed"); }
bool SlicePilotOnboardingFunnel::plugin_installed() { return get_flag("plugin_installed"); }
bool SlicePilotOnboardingFunnel::printer_bound() { return get_flag("printer_bound"); }
bool SlicePilotOnboardingFunnel::first_slice() { return get_flag("first_slice"); }
bool SlicePilotOnboardingFunnel::first_send() { return get_flag("first_send"); }
bool SlicePilotOnboardingFunnel::first_failure_seen() { return get_flag("first_failure_seen"); }
bool SlicePilotOnboardingFunnel::first_reprint() { return get_flag("first_reprint"); }
bool SlicePilotOnboardingFunnel::smart_analysis_done() { return get_flag("smart_analysis_done"); }

int SlicePilotOnboardingFunnel::completed_setup_steps()
{
    int n = 0;
    if (BambuSmartPrintService::is_bbl_printer_active())
        ++n;
    if (PrintReadinessGate::network_plugin_ready())
        ++n;
    if (PrintReadinessGate::has_bound_printer())
        ++n;
    if (Plater* plater = wxGetApp().plater()) {
        try {
            if (!plater->model().objects.empty())
                ++n;
        } catch (...) {
        }
    }
    return n;
}

wxString SlicePilotOnboardingFunnel::summary_text()
{
    wxString s;
    s << (wizard_completed() ? wxString::FromUTF8("✓ ") : wxString::FromUTF8("○ ")) << _L("Setup wizard") << "\n";
    s << (plugin_installed() ? wxString::FromUTF8("✓ ") : wxString::FromUTF8("○ ")) << _L("Network plug-in") << "\n";
    s << (printer_bound() ? wxString::FromUTF8("✓ ") : wxString::FromUTF8("○ ")) << _L("Printer bound") << "\n";
    s << (first_slice() ? wxString::FromUTF8("✓ ") : wxString::FromUTF8("○ ")) << _L("First slice") << "\n";
    s << (first_send() ? wxString::FromUTF8("✓ ") : wxString::FromUTF8("○ ")) << _L("First send") << "\n";
    s << (first_failure_seen() ? wxString::FromUTF8("✓ ") : wxString::FromUTF8("○ ")) << _L("Failure learned") << "\n";
    s << (first_reprint() ? wxString::FromUTF8("✓ ") : wxString::FromUTF8("○ ")) << _L("First Reprint");
    return s;
}

}} // namespace Slic3r::GUI
