#include "PrintEstimateSummary.hpp"

#include "libslic3r/Print.hpp"

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

const char* tier_label(ReadinessTier t, bool ko)
{
    switch (t) {
    case ReadinessTier::Excellent: return ko ? "우수" : "Excellent";
    case ReadinessTier::Good:      return ko ? "양호" : "Good";
    case ReadinessTier::Fair:      return ko ? "보통" : "Fair";
    default:                       return ko ? "주의 필요" : "Needs attention";
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

std::string PrintEstimateFormatter::format_compact_line(const PrintEstimateSummary& s, bool korean)
{
    if (!s.valid)
        return korean ? "예상 정보 없음" : "No estimate available";

    std::ostringstream ss;
    if (korean) {
        ss << "예상 " << (s.estimated_time.empty() ? "-" : s.estimated_time)
           << " · " << fmt(s.total_weight_g, 1) << " g"
           << " · " << fmt(s.total_cost, 2)
           << " · 성공률 " << int(std::round(s.success_rate)) << "%";
    } else {
        ss << "Est. " << (s.estimated_time.empty() ? "-" : s.estimated_time)
           << " · " << fmt(s.total_weight_g, 1) << " g"
           << " · " << fmt(s.total_cost, 2)
           << " · success " << int(std::round(s.success_rate)) << "%";
    }
    return ss.str();
}

std::string PrintEstimateFormatter::format_chat_block(const PrintEstimateSummary& s, bool korean)
{
    if (!s.valid)
        return korean ? "예상 정보를 사용할 수 없습니다." : "No print estimate available.";

    std::ostringstream ss;
    if (korean) {
        ss << "출력 예상\n";
        ss << "· 예상 시간: " << (s.estimated_time.empty() ? "-" : s.estimated_time) << "\n";
        ss << "· 필라멘트: " << fmt(s.filament_volume_cm3, 2) << " cm3 (" << fmt(s.total_weight_g, 1) << " g)\n";
        ss << "· 비용: " << fmt(s.total_cost, 2) << "\n";
        if (s.toolchanges > 0)
            ss << "· 도구 교체: " << s.toolchanges << "회\n";
        ss << "· 성공률: " << int(std::round(s.success_rate)) << "% (" << tier_label(s.readiness_tier, true) << ")\n";
        if (s.stability_known)
            ss << "· 전도 위험: " << int(std::round(s.tip_over_risk * 100.0)) << "%\n";
        if (!s.orientation_hint.empty())
            ss << "· 방향 제안: " << s.orientation_hint << "\n";
        if (!s.risk_factors.empty()) {
            ss << "· 위험 요소:\n";
            for (const std::string& r : s.risk_factors)
                ss << "   - " << r << "\n";
        }
    } else {
        ss << "Print estimate\n";
        ss << "- Estimated time: " << (s.estimated_time.empty() ? "-" : s.estimated_time) << "\n";
        ss << "- Filament: " << fmt(s.filament_volume_cm3, 2) << " cm3 (" << fmt(s.total_weight_g, 1) << " g)\n";
        ss << "- Cost: " << fmt(s.total_cost, 2) << "\n";
        if (s.toolchanges > 0)
            ss << "- Tool changes: " << s.toolchanges << "\n";
        ss << "- Success rate: " << int(std::round(s.success_rate)) << "% (" << tier_label(s.readiness_tier, false) << ")\n";
        if (s.stability_known)
            ss << "- Tip-over risk: " << int(std::round(s.tip_over_risk * 100.0)) << "%\n";
        if (!s.orientation_hint.empty())
            ss << "- Orientation hint: " << s.orientation_hint << "\n";
        if (!s.risk_factors.empty()) {
            ss << "- Risk factors:\n";
            for (const std::string& r : s.risk_factors)
                ss << "   * " << r << "\n";
        }
    }
    return ss.str();
}

} // namespace BambuSmartPrint
} // namespace Slic3r
