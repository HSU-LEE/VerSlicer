#include "FailureLogBundle.hpp"
#include "BambuSmartPrintPaths.hpp"
#include "ConfigSnapshot.hpp"

#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

bool copy_file_safe(const std::string& from, const std::string& to, std::vector<std::string>* errors)
{
    if (from.empty() || !fs::exists(from))
        return false;
    try {
        fs::create_directories(fs::path(to).parent_path());
        fs::copy_file(from, to, fs::copy_options::overwrite_existing);
        return true;
    } catch (const std::exception& e) {
        if (errors)
            errors->push_back(std::string("copy failed: ") + e.what());
        return false;
    }
}

} // namespace

FailureLogBundleResult FailureLogBundle::capture(const PrintFailureRecord& record,
                                                   const nlohmann::json* extra_printer_fields)
{
    FailureLogBundleResult result;
    ensure_smart_print_storage_ready();

    const std::string base = smart_print_data_dir() + "/failure_logs/" + record.record_id;
    result.bundle_dir = base;
    try {
        fs::create_directories(base);
    } catch (const std::exception& e) {
        result.errors.push_back(e.what());
        return result;
    }

    nlohmann::json manifest;
    manifest["record_id"] = record.record_id;
    manifest["timestamp_utc_ms"] = record.timestamp_utc_ms;
    manifest["printer_id"] = record.printer_id;
    manifest["printer_name"] = record.printer_name;
    manifest["printer_model"] = record.printer_model;
    manifest["job_id"] = record.job_id;
    manifest["gcode_file"] = record.gcode_file;
    manifest["mc_print_error_code"] = record.mc_print_error_code;
    manifest["print_error"] = record.print_error;
    manifest["config_snapshot_verified"] = record.config_snapshot_verified;
    manifest["plate_index"] = record.plate_index;
    manifest["diagnosis_title"] = record.diagnosis.title;

    result.config_json_path = base + "/slicing_settings.json";
    if (!atomic_write_text_file(result.config_json_path, ConfigSnapshot::to_json(record.config_snapshot)))
        result.errors.push_back("Could not write slicing_settings.json");
    else
        manifest["slicing_settings"] = "slicing_settings.json";

    nlohmann::json status;
    status["print_status"] = record.print_status;
    status["mc_print_stage"] = record.mc_print_stage;
    status["mc_print_percent"] = record.mc_print_percent;
    status["nozzle_temp"] = record.nozzle_temp;
    status["bed_temp"] = record.bed_temp;
    status["chamber_temp"] = record.chamber_temp;
    status["hms_codes"] = record.hms_codes;
    if (extra_printer_fields)
        for (auto it = extra_printer_fields->begin(); it != extra_printer_fields->end(); ++it)
            status[it.key()] = it.value();
    result.printer_status_path = base + "/printer_status.json";
    if (!atomic_write_text_file(result.printer_status_path, status.dump(2)))
        result.errors.push_back("Could not write printer_status.json");
    else
        manifest["printer_status"] = "printer_status.json";

    std::string gcode_src = record.gcode_file;
    if (!gcode_src.empty()) {
        result.gcode_path = base + "/job.gcode";
        if (!copy_file_safe(gcode_src, result.gcode_path, &result.errors)) {
            result.gcode_path = base + "/job.gcode.path.txt";
            atomic_write_text_file(result.gcode_path, gcode_src);
        }
        manifest["gcode"] = fs::path(result.gcode_path).filename().string();
    }

    result.camera_note_path = base + "/camera_capture.txt";
    const std::string cam_note =
        "Live camera frame is not stored automatically.\n"
        "Open the Device tab camera view at failure time and save a screenshot manually if needed.\n"
        "Record time (UTC ms): " + std::to_string(record.timestamp_utc_ms) + "\n";
    if (atomic_write_text_file(result.camera_note_path, cam_note))
        manifest["camera_note"] = "camera_capture.txt";

    result.manifest_path = base + "/manifest.json";
    result.success = atomic_write_text_file(result.manifest_path, manifest.dump(2));
    if (!result.success)
        result.errors.push_back("Could not write manifest.json");
    return result;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
