#ifndef slic3r_BambuSmartPrintWorkflowDialog_hpp_
#define slic3r_BambuSmartPrintWorkflowDialog_hpp_

#include "../MsgDialog.hpp"
#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"

namespace Slic3r { namespace GUI {

struct SmartPrintWorkflowContent
{
    std::string summary;
    std::string suggested_material;
    std::string diagnosis_title; // failure workflow title (not material)
    std::string prediction_summary;
    std::string readiness_headline;
    std::string active_filament;
    float       success_rate{ 0.f };
    bool        filament_mismatch{ false };
    int         complexity_score{ 0 };
    size_t      change_count{ 0 };
    std::vector<std::string> risk_factors;
    std::vector<std::string> change_preview; // key: reason (top N)
    std::vector<BambuSmartPrint::PrintInsight> insights;
    bool        show_success_gauge{ true };
    float       diagnosis_confidence{ 0.f }; // 0..1 when failure workflow
    bool        is_failure_workflow{ false };
    bool        is_smart_slice_result{ false };
    bool        diagnosis_uncertain{ false };
};

class BambuSmartPrintWorkflowDialog : public DPIDialog
{
public:
    BambuSmartPrintWorkflowDialog(wxWindow* parent, const SmartPrintWorkflowContent& content);

    bool apply_requested() const { return m_apply; }
    bool preview_requested() const { return m_preview; }

    /** Used when the dialog auto-confirms after the idle timeout. */
    void confirm_auto_apply();

    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    bool m_apply{ false };
    bool m_preview{ false };
};

}} // namespace

#endif
