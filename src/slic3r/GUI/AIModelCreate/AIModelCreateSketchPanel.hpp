#ifndef slic3r_AIModelCreateSketchPanel_hpp_
#define slic3r_AIModelCreateSketchPanel_hpp_

#include <vector>

#include <wx/panel.h>
#include <wx/gdicmn.h>

#include "libslic3r/Point.hpp"

namespace Slic3r { namespace GUI {

class AIModelCreateSketchPanel : public wxPanel
{
public:
    explicit AIModelCreateSketchPanel(wxWindow* parent);

    void clear_strokes();
    bool has_strokes() const { return !m_strokes.empty(); }

    std::vector<std::vector<Vec2d>> normalized_strokes() const;

private:
    void on_paint(wxPaintEvent& evt);
    void on_mouse(wxMouseEvent& evt);
    void on_erase(wxEraseEvent& evt) {}

    std::vector<std::vector<wxPoint>> m_strokes;
    std::vector<wxPoint>*              m_active_stroke{ nullptr };
    bool                                m_drawing{ false };
};

}} // namespace

#endif
