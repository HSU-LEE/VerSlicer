#ifndef slic3r_BambuSmartPrintPrivacyDialog_hpp_
#define slic3r_BambuSmartPrintPrivacyDialog_hpp_

#include "../MsgDialog.hpp"
#include <vector>

class Button;
class CheckBox;
class wxPanel;
class wxScrolledWindow;
class wxStaticText;

namespace Slic3r { namespace GUI {

class BambuSmartPrintPrivacyDialog : public DPIDialog
{
public:
    explicit BambuSmartPrintPrivacyDialog(wxWindow* parent);

    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void register_wrap_label(wxStaticText* label);
    void relayout_wrapped_content();
    void refresh_overview();
    void refresh_storage_details();
    void update_delete_button_state();

    wxScrolledWindow* m_scroll{ nullptr };
    wxStaticText*     m_hero_text{ nullptr };
    wxStaticText*     m_stat_failures{ nullptr };
    wxStaticText*     m_stat_successes{ nullptr };
    wxStaticText*     m_stat_recent{ nullptr };
    wxStaticText*     m_stat_printers{ nullptr };
    wxStaticText*     m_stat_disk{ nullptr };
    wxStaticText*     m_path_text{ nullptr };
    wxStaticText*     m_catalog_text{ nullptr };
    wxPanel*          m_load_error_banner{ nullptr };
    wxStaticText*     m_load_error_text{ nullptr };
    CheckBox*         m_delete_confirm_cb{ nullptr };
    Button*           m_btn_delete{ nullptr };

    std::vector<wxStaticText*> m_wrap_labels;

    std::string m_storage_path;
    std::string m_catalog_path;
};

}} // namespace

#endif
