#ifndef slic3r_BambuSmartPrintDeviceSummary_hpp_
#define slic3r_BambuSmartPrintDeviceSummary_hpp_

#include <wx/panel.h>
#include <wx/stattext.h>

class Button;

namespace Slic3r { namespace GUI {

class BambuSmartPrintDeviceSummary : public wxPanel
{
public:
    explicit BambuSmartPrintDeviceSummary(wxWindow* parent);
    void update_for_printer(const std::string& printer_id);

private:
    wxStaticText* m_summary{ nullptr };
    wxPanel*      m_failure_banner{ nullptr };
    wxStaticText* m_failure_text{ nullptr };
    Button*       m_failure_btn{ nullptr };
};

}} // namespace

#endif
