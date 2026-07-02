#ifndef slic3r_AIModelCreateDialog_hpp_
#define slic3r_AIModelCreateDialog_hpp_

#include "../GUI_Utils.hpp"
#include "AIModelCreateEngine.hpp"

namespace Slic3r { namespace GUI {

class AIModelCreateSketchPanel;

class AIModelCreateDialog : public DPIDialog
{
public:
    explicit AIModelCreateDialog(wxWindow* parent);

    void on_dpi_changed(const wxRect& suggested_rect) override;
    void show_and_raise();

private:
    void on_generate(wxCommandEvent&);
    void on_clear_sketch(wxCommandEvent&);
    void on_generation_done(AIModelCreateResult result);

    AIModelCreateSketchPanel* m_sketch{ nullptr };
    wxTextCtrl*               m_prompt{ nullptr };
    wxStaticText*             m_status{ nullptr };
    wxButton*                 m_generate_btn{ nullptr };
    bool                      m_busy{ false };
};

void show_ai_model_create_dialog();

}} // namespace

#endif
