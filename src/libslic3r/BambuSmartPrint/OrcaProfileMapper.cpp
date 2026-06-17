#include "OrcaProfileMapper.hpp"
#include "ConfigSnapshot.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r {
namespace BambuSmartPrint {

ProfileApplyResult OrcaProfileMapper::map_auto_result_to_config(const DynamicPrintConfig& base,
                                                                const AutoSettingsResult& auto_result)
{
    ProfileApplyResult out;
    out.merged_config = auto_result.config_delta;
    out.applied = ConfigSnapshot::diff(base, out.merged_config);
    out.summary = auto_result.summary;
    if (out.applied.empty() && !auto_result.changes.empty())
        out.applied = auto_result.changes;
    for (const SettingChange& ch : auto_result.changes) {
        if (!ch.key.empty() && !base.has(ch.key) && !out.merged_config.has(ch.key))
            out.skipped_keys.push_back(ch.key);
    }
    return out;
}

std::string OrcaProfileMapper::suggest_printer_preset_name(const PresetBundle& bundle,
                                                           const std::string& printer_model)
{
    if (printer_model.empty()) return {};
    std::string model = printer_model;
    boost::to_upper(model);

    std::string best;
    int best_score = 0;
    for (const Preset& p : bundle.printers) {
        if (!p.is_visible || !p.vendor || p.vendor->id != "BBL")
            continue;
        std::string name = p.name;
        boost::to_upper(name);
        int score = 0;
        if (name.find(model) != std::string::npos)
            score += 10;
        if (model.find("P2") != std::string::npos && name.find("P2") != std::string::npos)
            score += 8;
        if (model.find("P1") != std::string::npos && name.find("P1") != std::string::npos)
            score += 8;
        if (model.find("X1") != std::string::npos && name.find("X1") != std::string::npos)
            score += 8;
        if (model.find("A1") != std::string::npos && name.find("A1") != std::string::npos)
            score += 8;
        if (score > best_score) {
            best_score = score;
            best = p.name;
        }
    }
    return best;
}

bool OrcaProfileMapper::apply_printer_preset(PresetBundle& bundle, const std::string& preset_name)
{
    if (preset_name.empty()) return false;
    if (bundle.printers.find_preset(preset_name) == nullptr)
        return false;
    bundle.printers.select_preset_by_name(preset_name, true);
    return true;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
