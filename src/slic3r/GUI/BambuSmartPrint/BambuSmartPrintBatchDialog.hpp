#ifndef slic3r_BambuSmartPrintBatchDialog_hpp_
#define slic3r_BambuSmartPrintBatchDialog_hpp_

#include "../MsgDialog.hpp"
#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"

class wxListCtrl;
class wxStaticText;

namespace Slic3r { namespace GUI {

class BambuSmartPrintBatchDialog : public DPIDialog
{
public:
    BambuSmartPrintBatchDialog(wxWindow* parent, const BambuSmartPrint::PlateBatchSummary& summary);

    int selected_plate_index() const { return m_selected_plate; }

    bool open_plate_requested() const { return m_open_plate; }

    void confirm_auto_open();

    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void refresh_selection();

    const BambuSmartPrint::PlateBatchSummary& m_summary;
    wxListCtrl*   m_list{ nullptr };
    wxStaticText* m_detail{ nullptr };
    int           m_selected_plate{ -1 };
    bool          m_open_plate{ false };
};

}} // namespace

#endif
