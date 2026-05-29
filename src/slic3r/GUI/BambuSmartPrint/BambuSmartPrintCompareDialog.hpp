#ifndef slic3r_BambuSmartPrintCompareDialog_hpp_
#define slic3r_BambuSmartPrintCompareDialog_hpp_

#include "../GUI_Utils.hpp"
#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

class BambuSmartPrintCompareDialog : public DPIDialog
{
public:
    BambuSmartPrintCompareDialog(wxWindow* parent,
                                 const DynamicPrintConfig& before,
                                 const DynamicPrintConfig& after,
                                 const std::string& title,
                                 const std::vector<BambuSmartPrint::SettingChange>* change_reasons = nullptr,
                                 bool approval_mode = false);

    void on_dpi_changed(const wxRect& suggested_rect) override;
};

}} // namespace

#endif
