#ifndef slic3r_AutoSettingsRules_hpp_
#define slic3r_AutoSettingsRules_hpp_

namespace Slic3r {
namespace BambuSmartPrint {

struct AutoSettingsThresholds {
    double overhang_moderate{ 0.14 };
    double overhang_steep{ 0.18 };
    double overhang_high{ 0.30 };
    double overhang_fan{ 0.18 };
    double first_layer_contact_low{ 0.30 };
    double first_layer_brim{ 0.28 };
    double tall_height_mm{ 100.0 };
    double very_tall_height_mm{ 180.0 };
    double large_volume_mm3{ 500000.0 };
    double large_flat_xy_mm{ 140.0 };
    int    complexity_thin_wall{ 65 };
    // SliceGeometryAnalyzer-driven rules
    float slice_overhang_support{ 0.08f };
    float slice_overhang_moderate{ 0.12f };
    float slice_overhang_steep{ 0.22f };
    int   slice_islands_moderate{ 3 };
    int   slice_islands_high{ 8 };
    float slice_bridge_slow_mm{ 15.f };
    float slice_bridge_high_mm{ 18.f };
};

const AutoSettingsThresholds& auto_settings_thresholds();

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
