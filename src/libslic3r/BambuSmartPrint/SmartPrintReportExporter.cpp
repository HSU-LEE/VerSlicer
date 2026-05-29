#include "SmartPrintReportExporter.hpp"
#include "BambuSmartPrintPaths.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <iomanip>
#include <sstream>

namespace Slic3r {
namespace BambuSmartPrint {

using json = nlohmann::json;

static std::string iso_timestamp_utc()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string SmartPrintReportExporter::build_report_json(const ModelAnalysis& mesh,
                                                        const ReadinessReport& readiness,
                                                        const AutoSettingsResult& settings,
                                                        const SuccessPrediction& prediction,
                                                        int plate_index,
                                                        const PlateBatchSummary* batch,
                                                        const SliceAnalysis* slice)
{
    json root;
    root["app"]        = "Verslicer Smart Print";
    root["generated"]  = iso_timestamp_utc();
    if (plate_index >= 0)
        root["plate_index"] = plate_index;

    root["readiness"] = {
        {"score", readiness.score},
        {"success_rate", readiness.success_rate},
        {"headline", readiness.headline},
        {"filament_mismatch", readiness.filament_mismatch},
        {"active_filament", readiness.active_filament_hint},
        {"suggested_filament", readiness.suggested_filament_hint},
    };

    root["geometry"] = {
        {"volume_mm3", mesh.volume_mm3},
        {"height_mm", mesh.height_mm},
        {"max_xy_mm", mesh.max_xy_mm},
        {"complexity_score", mesh.complexity_score},
        {"suggested_material", mesh.suggested_material},
        {"needs_brim", mesh.needs_brim},
        {"tall_narrow", mesh.tall_narrow},
    };

    root["prediction"] = {
        {"success_rate", prediction.success_rate},
        {"summary", prediction.summary},
        {"risk_factors", prediction.risk_factors},
    };

    if (slice && slice->valid) {
        root["slice_analysis"] = {
            {"overhang_area_ratio", slice->overhang_area_ratio},
            {"unsupported_islands", slice->unsupported_islands_count},
            {"bridge_length_max_mm", slice->bridge_length_max_mm},
            {"risk_notes", slice->risk_notes},
        };
    }

    json changes = json::array();
    for (const SettingChange& ch : settings.changes)
        changes.push_back({ {"key", ch.key}, {"from", ch.old_value}, {"to", ch.new_value}, {"reason", ch.reason} });
    root["suggested_changes"] = changes;
    json blocked = json::array();
    for (const SettingChange& ch : settings.blocked_changes)
        blocked.push_back({ {"key", ch.key}, {"reason", ch.reason} });
    if (!blocked.empty())
        root["blocked_changes"] = blocked;
    root["summary"]           = settings.summary;

    json insights = json::array();
    for (const PrintInsight& ins : readiness.insights)
        insights.push_back({ {"label", ins.label}, {"detail", ins.detail} });
    root["insights"] = insights;
    root["action_items"] = readiness.action_items;

    if (batch) {
        json plates = json::array();
        for (const PlateBatchEntry& e : batch->plates) {
            if (e.empty)
                continue;
            plates.push_back({
                {"plate", e.plate_index + 1},
                {"readiness", e.readiness_score},
                {"priority", e.priority_score},
                {"changes", e.change_count},
                {"material", e.suggested_material},
                {"filament_family", e.active_filament_family},
                {"complexity", e.complexity_score},
            });
        }
        root["batch"] = {
            {"plates", plates},
            {"average_readiness", batch->average_readiness},
            {"total_changes", batch->total_suggested_changes},
            {"best_plate", batch->best_plate_index >= 0 ? batch->best_plate_index + 1 : 0},
            {"filament_conflict", batch->filament_conflict_note},
        };
    }

    return root.dump(2);
}

bool SmartPrintReportExporter::write_report_file(const std::string& path,
                                                 const ModelAnalysis& mesh,
                                                 const ReadinessReport& readiness,
                                                 const AutoSettingsResult& settings,
                                                 const SuccessPrediction& prediction,
                                                 int plate_index,
                                                 const PlateBatchSummary* batch,
                                                 std::string* error_out,
                                                 const SliceAnalysis* slice)
{
    try {
        const std::string body = build_report_json(mesh, readiness, settings, prediction, plate_index, batch, slice);
        if (!atomic_write_text_file(path, body)) {
            if (error_out)
                *error_out = "Failed to write report file";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (error_out)
            *error_out = e.what();
        return false;
    }
}

} // namespace BambuSmartPrint
} // namespace Slic3r
