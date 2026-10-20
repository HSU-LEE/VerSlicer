#include "PrintEstimateSummary.hpp"

#include "libslic3r/Print.hpp"
#include "libslic3r/I18N.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

std::string fmt(double value, int precision)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    return ss.str();
}

std::string tier_label(ReadinessTier t)
{
    switch (t) {
    case ReadinessTier::Excellent: return _u8L("Excellent");
    case ReadinessTier::Good:      return _u8L("Good");
    case ReadinessTier::Fair:      return _u8L("Fair");
    default:                       return _u8L("Needs attention");
    }
}

} // namespace

PrintEstimateSummary PrintEstimateCollector::collect(const PrintStatistics& stats,
                                                     const GeometryAssessment& assessment,
                                                     const DynamicPrintConfig& /*config*/)
{
    PrintEstimateSummary s;
    s.valid = true;

    s.estimated_time      = stats.estimated_normal_print_time;
    s.total_weight_g      = stats.total_weight;
    s.total_cost          = stats.total_cost;
    s.filament_used_mm    = stats.total_used_filament;
    s.filament_volume_cm3 = stats.total_extruded_volume / 1000.0; // mm3 -> cm3
    s.toolchanges         = stats.total_toolchanges;

    s.success_rate    = assessment.prediction.success_rate;
    s.readiness_tier  = assessment.readiness.tier;
    s.stability_known = assessment.stability.computed && assessment.stability.com_known;
    s.tip_over_risk   = assessment.stability.tip_over_risk;
    s.orientation_hint = assessment.orientation_hint;
    s.risk_factors    = assessment.prediction.risk_factors;

    return s;
}

std::string PrintEstimateFormatter::format_compact_line(const PrintEstimateSummary& s, bool /*korean*/)
{
    if (!s.valid)
        return _u8L("No estimate available");

    std::ostringstream ss;
    ss << _u8L("Est.") << " " << (s.estimated_time.empty() ? "-" : s.estimated_time)
       << " · " << fmt(s.total_weight_g, 1) << " g"
       << " · " << fmt(s.total_cost, 2)
       << " · " << _u8L("success") << " " << int(std::round(s.success_rate)) << "%";
    return ss.str();
}

std::string PrintEstimateFormatter::format_chat_block(const PrintEstimateSummary& s, bool /*korean*/)
{
    if (!s.valid)
        return _u8L("No print estimate available.");

    std::ostringstream ss;
    ss << _u8L("Print estimate") << "\n";
    ss << "- " << _u8L("Estimated time:") << " " << (s.estimated_time.empty() ? "-" : s.estimated_time) << "\n";
    ss << "- " << _u8L("Filament:") << " " << fmt(s.filament_volume_cm3, 2) << " cm3 (" << fmt(s.total_weight_g, 1) << " g)\n";
    ss << "- " << _u8L("Cost:") << " " << fmt(s.total_cost, 2) << "\n";
    if (s.toolchanges > 0)
        ss << "- " << _u8L("Tool changes:") << " " << s.toolchanges << "\n";
    ss << "- " << _u8L("Success rate:") << " " << int(std::round(s.success_rate)) << "% (" << tier_label(s.readiness_tier) << ")\n";
    if (s.stability_known)
        ss << "- " << _u8L("Tip-over risk:") << " " << int(std::round(s.tip_over_risk * 100.0)) << "%\n";
    if (!s.orientation_hint.empty())
        ss << "- " << _u8L("Orientation hint:") << " " << s.orientation_hint << "\n";
    if (!s.risk_factors.empty()) {
        ss << "- " << _u8L("Risk factors:") << "\n";
        for (const std::string& r : s.risk_factors)
            ss << "   * " << r << "\n";
    }
    return ss.str();
}

} // namespace BambuSmartPrint
} // namespace Slic3r
