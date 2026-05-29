#include "FailureAnalyzer.hpp"
#include "BambuErrorCatalog.hpp"
#include "SettingsOptimizer.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

FailureDiagnosis FailureAnalyzer::analyze_record(const PrintFailureRecord& record)
{
    FailureDiagnosis d = BambuErrorCatalog::diagnose(
        record.mc_print_error_code, record.print_error, record.hms_codes);

    std::vector<std::string> hms_causes;
    for (const std::string& c : d.likely_causes) {
        if (c.size() >= 4 && c.compare(0, 4, "HMS:") == 0)
            hms_causes.push_back(c);
    }

    const FailureCategory before = d.category;
    if (record.mc_print_percent < 15 && d.category == FailureCategory::Unknown)
        d.category = FailureCategory::Adhesion;

    if (record.mc_print_percent > 80 && d.category == FailureCategory::Unknown)
        d.category = FailureCategory::Filament;

    if (d.category != before) {
        d.recommended_fixes.clear();
        d.likely_causes.clear();
        BambuErrorCatalog::apply_category_fixes(d);
        for (const std::string& h : hms_causes)
            d.likely_causes.push_back(h);
    }

    if (d.recommended_fixes.empty() && (d.confidence < 0.7f || d.category == FailureCategory::Unknown))
        BambuErrorCatalog::apply_category_fixes(d);

    return d;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
