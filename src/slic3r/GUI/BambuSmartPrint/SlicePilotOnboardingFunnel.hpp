#ifndef slic3r_SlicePilotOnboardingFunnel_hpp_
#define slic3r_SlicePilotOnboardingFunnel_hpp_

#include <wx/string.h>

namespace Slic3r {
class AppConfig;

namespace GUI {

class SlicePilotOnboardingFunnel
{
public:
    static void initialize_defaults(AppConfig* cfg);

    static void record_wizard_completed();
    static void record_plugin_installed();
    static void record_printer_bound();
    static void record_first_slice();
    static void record_first_send();
    static void record_first_failure_seen();
    static void record_first_reprint();
    static void record_smart_analysis_done();

    static bool wizard_completed();
    static bool plugin_installed();
    static bool printer_bound();
    static bool first_slice();
    static bool first_send();
    static bool first_failure_seen();
    static bool first_reprint();
    static bool smart_analysis_done();

    static int  completed_setup_steps();
    static wxString summary_text();
};

}} // namespace Slic3r::GUI

#endif
