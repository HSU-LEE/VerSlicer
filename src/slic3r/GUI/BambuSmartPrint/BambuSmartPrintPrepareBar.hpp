#ifndef slic3r_BambuSmartPrintPrepareBar_hpp_
#define slic3r_BambuSmartPrintPrepareBar_hpp_

#include <wx/panel.h>

class wxStaticText;
class Button;

namespace Slic3r { namespace GUI {

class SlicePilotSetupHub;

class BambuSmartPrintPrepareBar : public wxPanel
{
public:
    explicit BambuSmartPrintPrepareBar(wxWindow* parent);
    ~BambuSmartPrintPrepareBar();

    void refresh();
    void on_color_mode_changed(bool is_dark);

private:
    void refresh_action_state();
    void bind_actions();
    void update_failure_action_card();
    void show_more_menu();

    wxStaticText* m_status{ nullptr };
    wxStaticText* m_failure_hint{ nullptr };
    wxStaticText* m_first_failure_detail{ nullptr };
    Button*       m_failure_reprint_btn{ nullptr };
    Button*       m_filament_fix_btn{ nullptr };
    Button*       m_print_btn{ nullptr };
    Button*       m_ai_btn{ nullptr };
    Button*       m_more_btn{ nullptr };
    SlicePilotSetupHub* m_setup_hub{ nullptr };
};

}} // namespace

#endif
