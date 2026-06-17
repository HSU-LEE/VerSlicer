#include "BambuSmartPrintJson.hpp"
#include <nlohmann/json.hpp>

namespace Slic3r {
namespace BambuSmartPrint {

using json = nlohmann::json;

static const char* category_to_string(FailureCategory c)
{
    switch (c) {
    case FailureCategory::Adhesion: return "adhesion";
    case FailureCategory::Filament: return "filament";
    case FailureCategory::Temperature: return "temperature";
    case FailureCategory::Mechanical: return "mechanical";
    case FailureCategory::Gcode: return "gcode";
    case FailureCategory::Network: return "network";
    case FailureCategory::UserCancelled: return "user_cancelled";
    default: return "unknown";
    }
}

static FailureCategory category_from_string(const std::string& s)
{
    if (s == "adhesion") return FailureCategory::Adhesion;
    if (s == "filament") return FailureCategory::Filament;
    if (s == "temperature") return FailureCategory::Temperature;
    if (s == "mechanical") return FailureCategory::Mechanical;
    if (s == "gcode") return FailureCategory::Gcode;
    if (s == "network") return FailureCategory::Network;
    if (s == "user_cancelled") return FailureCategory::UserCancelled;
    return FailureCategory::Unknown;
}

json diagnosis_to_json(const FailureDiagnosis& d)
{
    json j;
    j["category"]   = category_to_string(d.category);
    j["title"]      = d.title;
    j["description"]= d.description;
    j["confidence"] = d.confidence;
    j["likely_causes"] = d.likely_causes;
    json fixes = json::array();
    for (const SettingChange& c : d.recommended_fixes) {
        fixes.push_back({ {"key", c.key}, {"old_value", c.old_value}, {"new_value", c.new_value}, {"reason", c.reason} });
    }
    j["recommended_fixes"] = fixes;
    return j;
}

FailureDiagnosis diagnosis_from_json(const json& j)
{
    FailureDiagnosis d;
    if (j.contains("category"))
        d.category = category_from_string(j["category"].get<std::string>());
    d.title       = j.value("title", "");
    d.description = j.value("description", "");
    d.confidence  = j.value("confidence", 0.f);
    if (j.contains("likely_causes"))
        for (const auto& c : j["likely_causes"])
            d.likely_causes.push_back(c.get<std::string>());
    if (j.contains("recommended_fixes")) {
        for (const auto& f : j["recommended_fixes"]) {
            SettingChange ch;
            ch.key        = f.value("key", "");
            ch.old_value  = f.value("old_value", "");
            ch.new_value  = f.value("new_value", "");
            ch.reason     = f.value("reason", "");
            d.recommended_fixes.push_back(std::move(ch));
        }
    }
    return d;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
