#ifndef slic3r_SlicePilotSetupHub_hpp_
#define slic3r_SlicePilotSetupHub_hpp_

#include <wx/panel.h>

class wxStaticText;
class Button;

namespace Slic3r { namespace GUI {

class Plater;

enum class SetupHubStep : int {
    Printer = 0,
    Plugin  = 1,
    Connect = 2,
    Model   = 3,
    Count   = 4,
};

class SlicePilotSetupHub : public wxPanel
{
public:
    explicit SlicePilotSetupHub(wxWindow* parent);
    ~SlicePilotSetupHub();

    static bool is_enabled();
    static int  completed_count();
    static bool step_complete(SetupHubStep step);
    static void refresh_all(Plater* plater);
    static void activate_step(SetupHubStep step, Plater* plater);
    static void highlight_step(SetupHubStep step);
    static wxString print_button_label();

    /** Returns true if gate passed or user chose slice-only; false if cancelled. */
    static bool handle_print_gate(Plater* plater, wxWindow* parent, bool& slice_only);

    void refresh(Plater* plater);

private:
    void bind_step_buttons();
    void update_step_buttons(Plater* plater);

    Button*       m_step_btns[int(SetupHubStep::Count)]{ nullptr };
    wxStaticText* m_summary{ nullptr };
};

}} // namespace Slic3r::GUI

#endif
