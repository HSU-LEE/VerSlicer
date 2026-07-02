#include "AIModelCreateSketchPanel.hpp"

#include "../wxExtensions.hpp"

#include <wx/dcbuffer.h>

namespace Slic3r { namespace GUI {

AIModelCreateSketchPanel::AIModelCreateSketchPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(420), FromDIP(280)), wxBORDER_SIMPLE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);
    SetMinSize(FromDIP(wxSize(320, 200)));

    Bind(wxEVT_PAINT, &AIModelCreateSketchPanel::on_paint, this);
    Bind(wxEVT_LEFT_DOWN, &AIModelCreateSketchPanel::on_mouse, this);
    Bind(wxEVT_LEFT_UP, &AIModelCreateSketchPanel::on_mouse, this);
    Bind(wxEVT_MOTION, &AIModelCreateSketchPanel::on_mouse, this);
    Bind(wxEVT_ERASE_BACKGROUND, &AIModelCreateSketchPanel::on_erase, this);
}

void AIModelCreateSketchPanel::clear_strokes()
{
    m_strokes.clear();
    m_active_stroke = nullptr;
    m_drawing       = false;
    Refresh();
}

std::vector<std::vector<Vec2d>> AIModelCreateSketchPanel::normalized_strokes() const
{
    const wxSize sz = GetClientSize();
    if (sz.x <= 0 || sz.y <= 0)
        return {};

    std::vector<std::vector<Vec2d>> out;
    out.reserve(m_strokes.size());
    for (const auto& stroke : m_strokes) {
        if (stroke.size() < 2)
            continue;
        std::vector<Vec2d> norm;
        norm.reserve(stroke.size());
        for (const wxPoint& p : stroke)
            norm.emplace_back(double(p.x) / double(sz.x), double(p.y) / double(sz.y));
        out.push_back(std::move(norm));
    }
    return out;
}

void AIModelCreateSketchPanel::on_paint(wxPaintEvent& /*evt*/)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    const wxSize sz = GetClientSize();
    dc.SetPen(wxPen(wxColour(220, 220, 220), 1, wxPENSTYLE_DOT));
    for (int i = 1; i < 4; ++i) {
        const int x = sz.x * i / 4;
        const int y = sz.y * i / 4;
        dc.DrawLine(x, 0, x, sz.y);
        dc.DrawLine(0, y, sz.x, y);
    }

    dc.SetPen(wxPen(wxColour(0xFF, 0x8F, 0x4A), FromDIP(3), wxPENSTYLE_SOLID));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    for (const auto& stroke : m_strokes) {
        if (stroke.size() < 2)
            continue;
        for (size_t i = 1; i < stroke.size(); ++i)
            dc.DrawLine(stroke[i - 1], stroke[i]);
    }
}

void AIModelCreateSketchPanel::on_mouse(wxMouseEvent& evt)
{
    if (evt.LeftDown()) {
        m_strokes.emplace_back();
        m_active_stroke = &m_strokes.back();
        m_active_stroke->push_back(evt.GetPosition());
        m_drawing = true;
        CaptureMouse();
        Refresh();
        return;
    }

    if (evt.LeftUp()) {
        if (m_drawing && HasCapture())
            ReleaseMouse();
        m_drawing       = false;
        m_active_stroke = nullptr;
        Refresh();
        return;
    }

    if (evt.Dragging() && m_drawing && m_active_stroke != nullptr) {
        const wxPoint p = evt.GetPosition();
        if (m_active_stroke->empty() || m_active_stroke->back() != p) {
            m_active_stroke->push_back(p);
            Refresh();
        }
    }
}

}} // namespace
