#ifndef slic3r_OllamaChatDialog_hpp_
#define slic3r_OllamaChatDialog_hpp_

#include "../GUI_Utils.hpp"

namespace Slic3r { namespace GUI {

class OllamaChatPanel;

class OllamaChatDialog : public DPIDialog
{
public:
    explicit OllamaChatDialog(wxWindow* parent);

    void on_dpi_changed(const wxRect& suggested_rect) override;

    void toggle();
    void ensure_visible_near_canvas();
    void submit_text_and_send(const wxString& text);
    void set_input_text(const wxString& text);

private:
    OllamaChatPanel* m_panel{nullptr};
};

}} // namespace

#endif

