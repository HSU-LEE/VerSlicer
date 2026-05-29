#ifndef slic3r_MaterialAdvisor_hpp_
#define slic3r_MaterialAdvisor_hpp_

#include "BambuSmartPrintTypes.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class MaterialAdvisor
{
public:
    // Normalized family: PLA, PETG, ABS, ASA, TPU, PA, PC, or empty if unknown.
    static std::string detect_filament_family(const DynamicPrintConfig& config, int extruder_id = 0);
    static bool families_compatible(const std::string& active_family, const std::string& suggested);
    static void annotate_filament_readiness(const ModelAnalysis& model, const DynamicPrintConfig& config,
                                            ReadinessReport& report);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
