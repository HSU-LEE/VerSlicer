#include "AutoSettingsRules.hpp"
#include "BambuSmartPrintPaths.hpp"

#include "libslic3r/Utils.hpp"
#include "nlohmann/json.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <mutex>

namespace Slic3r {
namespace BambuSmartPrint {

namespace fs = boost::filesystem;
using json = nlohmann::json;

static AutoSettingsThresholds& thresholds_mut()
{
    static AutoSettingsThresholds t;
    static std::once_flag once;
    std::call_once(once, []() {
        auto load = [](const fs::path& p) {
            if (!fs::exists(p))
                return;
            try {
                boost::nowide::ifstream ifs(p.string());
                json root;
                ifs >> root;
                if (root.contains("overhang_moderate"))
                    t.overhang_moderate = root["overhang_moderate"].get<double>();
                if (root.contains("overhang_steep"))
                    t.overhang_steep = root["overhang_steep"].get<double>();
                if (root.contains("overhang_high"))
                    t.overhang_high = root["overhang_high"].get<double>();
                if (root.contains("overhang_fan"))
                    t.overhang_fan = root["overhang_fan"].get<double>();
                if (root.contains("first_layer_contact_low"))
                    t.first_layer_contact_low = root["first_layer_contact_low"].get<double>();
                if (root.contains("first_layer_brim"))
                    t.first_layer_brim = root["first_layer_brim"].get<double>();
                if (root.contains("tall_height_mm"))
                    t.tall_height_mm = root["tall_height_mm"].get<double>();
                if (root.contains("very_tall_height_mm"))
                    t.very_tall_height_mm = root["very_tall_height_mm"].get<double>();
                if (root.contains("large_volume_mm3"))
                    t.large_volume_mm3 = root["large_volume_mm3"].get<double>();
                if (root.contains("large_flat_xy_mm"))
                    t.large_flat_xy_mm = root["large_flat_xy_mm"].get<double>();
                if (root.contains("complexity_thin_wall"))
                    t.complexity_thin_wall = root["complexity_thin_wall"].get<int>();
                if (root.contains("slice_overhang_support"))
                    t.slice_overhang_support = root["slice_overhang_support"].get<float>();
                if (root.contains("slice_overhang_moderate"))
                    t.slice_overhang_moderate = root["slice_overhang_moderate"].get<float>();
                if (root.contains("slice_overhang_steep"))
                    t.slice_overhang_steep = root["slice_overhang_steep"].get<float>();
                if (root.contains("slice_islands_moderate"))
                    t.slice_islands_moderate = root["slice_islands_moderate"].get<int>();
                if (root.contains("slice_islands_high"))
                    t.slice_islands_high = root["slice_islands_high"].get<int>();
                if (root.contains("slice_bridge_slow_mm"))
                    t.slice_bridge_slow_mm = root["slice_bridge_slow_mm"].get<float>();
                if (root.contains("slice_bridge_high_mm"))
                    t.slice_bridge_high_mm = root["slice_bridge_high_mm"].get<float>();
            } catch (...) {}
        };
        load(fs::path(resources_dir()) / "bambu_smart_print" / "auto_settings_rules.json");
        load(fs::path(smart_print_data_dir()) / "auto_settings_rules.json");
    });
    return t;
}

const AutoSettingsThresholds& auto_settings_thresholds()
{
    return thresholds_mut();
}

} // namespace BambuSmartPrint
} // namespace Slic3r
