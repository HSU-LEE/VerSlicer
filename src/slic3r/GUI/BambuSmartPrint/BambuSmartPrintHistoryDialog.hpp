#ifndef slic3r_BambuSmartPrintHistoryDialog_hpp_
#define slic3r_BambuSmartPrintHistoryDialog_hpp_

#include "../MsgDialog.hpp"
#include "libslic3r/BambuSmartPrint/FailureDatabase.hpp"
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <vector>
#include <string>

class Button;

namespace Slic3r { namespace GUI {

class BambuSmartPrintHistoryDialog : public DPIDialog
{
public:
    BambuSmartPrintHistoryDialog(wxWindow* parent);

    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void refresh_overview_stats();
    void refresh_failures(bool keep_selection = false);
    void refresh_successes();
    void show_failure_detail(const std::string& record_id);
    void show_success_detail(size_t success_index);
    void on_failure_selection(wxListEvent& evt);
    void on_success_selection(wxListEvent& evt);
    void on_helpful(wxCommandEvent&);
    void on_not_helpful(wxCommandEvent&);
    void on_reapply(wxCommandEvent&);

    wxNotebook*   m_notebook{ nullptr };
    wxListCtrl*   m_fail_list{ nullptr };
    wxListCtrl*   m_success_list{ nullptr };
    wxStaticText* m_detail{ nullptr };
    wxStaticText* m_stat_failures{ nullptr };
    wxStaticText* m_stat_successes{ nullptr };
    wxStaticText* m_stat_recent{ nullptr };
    Button*       m_btn_helpful{ nullptr };
    Button*       m_btn_not{ nullptr };
    Button*       m_btn_reapply{ nullptr };
    std::string   m_selected_record_id;
    std::vector<std::string> m_row_record_ids;
    std::vector<BambuSmartPrint::FailureDatabase::SuccessRecord> m_success_rows;
};

}} // namespace

#endif
