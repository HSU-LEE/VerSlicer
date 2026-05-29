#ifndef slic3r_BambuSmartPrintDialog_hpp_
#define slic3r_BambuSmartPrintDialog_hpp_

#include "../GUI_Utils.hpp"

namespace Slic3r { namespace GUI {

class BambuSmartPrintPanel;

class BambuSmartPrintDialog : public DPIDialog
{
public:
    explicit BambuSmartPrintDialog(wxWindow* parent);

    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    BambuSmartPrintPanel* m_panel{ nullptr };
};

}} // namespace

#endif
