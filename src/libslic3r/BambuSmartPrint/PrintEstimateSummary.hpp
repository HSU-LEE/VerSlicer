#ifndef slic3r_PrintEstimateSummary_hpp_
#define slic3r_PrintEstimateSummary_hpp_

#include "AutoConfigEngine.hpp"     // GeometryAssessment
#include "BambuSmartPrintTypes.hpp" // ReadinessTier

#include "libslic3r/PrintConfig.hpp"

#include <string>
#include <vector>

namespace Slic3r {

struct PrintStatistics;

namespace BambuSmartPrint {

/** Compact, headless summary combining slice statistics with geometry-derived risk. */
struct PrintEstimateSummary {
    bool        valid{ false };

    // From PrintStatistics.
    std::string estimated_time;        // human-readable string from the slicer
    double      total_weight_g{ 0.0 };
    double      total_cost{ 0.0 };
    double      filament_used_mm{ 0.0 };
    double      filament_volume_cm3{ 0.0 };
    int         toolchanges{ 0 };

    // From GeometryAssessment.
    float         success_rate{ 0.f }; // 0..100
    ReadinessTier readiness_tier{ ReadinessTier::Fair };
    bool          stability_known{ false };
    double        tip_over_risk{ 0.0 };
    std::string   orientation_hint;
    std::vector<std::string> risk_factors;
};

class PrintEstimateCollector
{
public:
    static PrintEstimateSummary collect(const PrintStatistics& stats,
                                        const GeometryAssessment& assessment,
                                        const DynamicPrintConfig& config);
};

class PrintEstimateFormatter
{
public:
    /** Multi-line chat block (KO when korean == true, otherwise EN). */
    static std::string format_chat_block(const PrintEstimateSummary& summary, bool korean);
    /** One-line compact summary (KO/EN). */
    static std::string format_compact_line(const PrintEstimateSummary& summary, bool korean);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
