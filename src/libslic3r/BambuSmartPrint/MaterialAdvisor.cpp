#include "MaterialAdvisor.hpp"
#include "PrintReadinessEngine.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

static std::string upper_ascii(std::string s)
{
    for (char& c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static std::string family_from_string(const std::string& raw)
{
    const std::string u = upper_ascii(raw);
    if (u.find("PLA") != std::string::npos) return "PLA";
    if (u.find("PETG") != std::string::npos) return "PETG";
    if (u.find("ABS") != std::string::npos && u.find("ASA") == std::string::npos) return "ABS";
    if (u.find("ASA") != std::string::npos) return "ASA";
    if (u.find("TPU") != std::string::npos || u.find("TPE") != std::string::npos) return "TPU";
    if (u.find("PA") != std::string::npos || u.find("NYLON") != std::string::npos) return "PA";
    if (u.find("PC") != std::string::npos || u.find("POLYCARBONATE") != std::string::npos) return "PC";
    return {};
}

} // namespace

std::string MaterialAdvisor::detect_filament_family(const DynamicPrintConfig& config, int extruder_id)
{
    if (config.has("filament_type")) {
        if (const ConfigOptionStrings* types = config.option<ConfigOptionStrings>("filament_type")) {
            if (extruder_id >= 0 && extruder_id < int(types->values.size()) && !types->values[extruder_id].empty())
                return family_from_string(types->values[extruder_id]);
            for (const std::string& t : types->values)
                if (!t.empty())
                    return family_from_string(t);
        }
    }
    return {};
}

bool MaterialAdvisor::families_compatible(const std::string& active_family, const std::string& suggested)
{
    if (active_family.empty() || suggested.empty())
        return true;
    if (active_family == suggested)
        return true;
    if ((active_family == "PLA" || active_family == "PETG") && (suggested == "PLA" || suggested == "PETG"))
        return true;
    if ((active_family == "ABS" || active_family == "ASA") && (suggested == "ABS" || suggested == "ASA"))
        return true;
    return false;
}

void MaterialAdvisor::annotate_filament_readiness(const ModelAnalysis& model, const DynamicPrintConfig& config,
                                                  ReadinessReport& report)
{
    report.active_filament_hint    = detect_filament_family(config);
    report.suggested_filament_hint = model.suggested_material;
    report.filament_mismatch = !families_compatible(report.active_filament_hint, report.suggested_filament_hint);

    if (report.filament_mismatch && !report.active_filament_hint.empty()) {
        report.score = std::max(5.f, report.score - 8.f);
        report.tier  = PrintReadinessEngine::tier_from_score(report.score);
        PrintInsight ins;
        ins.label    = "Filament";
        ins.detail   = "Active " + report.active_filament_hint + " vs suggested "
                     + report.suggested_filament_hint + " — verify preset before printing";
        ins.severity = RiskSeverity::High;
        report.insights.insert(report.insights.begin(), ins);
        report.action_items.insert(report.action_items.begin(),
            "Switch filament preset to " + report.suggested_filament_hint + " or confirm material choice");
    }
}

} // namespace BambuSmartPrint
} // namespace Slic3r
