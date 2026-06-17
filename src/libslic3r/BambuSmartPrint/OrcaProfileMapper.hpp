#ifndef slic3r_OrcaProfileMapper_hpp_
#define slic3r_OrcaProfileMapper_hpp_

#include "BambuSmartPrintTypes.hpp"
#include "libslic3r/PresetBundle.hpp"
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

struct ProfileApplyResult {
    DynamicPrintConfig merged_config;
    std::vector<SettingChange> applied;
    std::vector<std::string> skipped_keys;
    std::string summary;
};

class OrcaProfileMapper
{
public:
    // Merge AI/auto deltas onto Orca print+plate config (same keys as Plater apply).
    static ProfileApplyResult map_auto_result_to_config(const DynamicPrintConfig& base,
                                                        const AutoSettingsResult& auto_result);

    // Pick closest Bambu printer preset name for a device model string (e.g. C12, P1S).
    static std::string suggest_printer_preset_name(const PresetBundle& bundle, const std::string& printer_model);

    // Apply printer preset by name if found (returns false if missing).
    static bool apply_printer_preset(PresetBundle& bundle, const std::string& preset_name);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
