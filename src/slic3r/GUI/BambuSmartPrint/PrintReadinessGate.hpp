#ifndef slic3r_PrintReadinessGate_hpp_
#define slic3r_PrintReadinessGate_hpp_

#include <wx/string.h>

namespace Slic3r { namespace GUI {

class Plater;

enum class PrintGateResult {
    Proceed,
    ProceedSliceOnly,
    Cancelled,
};

class PrintReadinessGate
{
public:
    static PrintGateResult run(Plater* plater, wxWindow* parent);

    static bool network_plugin_ready();
    static bool has_bound_printer();
    static bool try_fix_filament_mismatch(Plater* plater);
    static wxString filament_fix_button_label();
};

}} // namespace Slic3r::GUI

#endif
