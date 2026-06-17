#include "FailureDatabase.hpp"
#include "BambuSmartPrintJson.hpp"
#include "BambuSmartPrintPaths.hpp"
#include "ConfigSnapshot.hpp"

#include "libslic3r/Utils.hpp"
#include "nlohmann/json.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <chrono>
#include <map>
#include <algorithm>
#include <stdexcept>

namespace Slic3r {
namespace BambuSmartPrint {

using json = nlohmann::json;
namespace fs = boost::filesystem;

static void atomic_write_json(const std::string& path, const json& root)
{
    if (!atomic_write_text_file(path, root.dump(2)))
        throw std::runtime_error("atomic_write_text_file failed for " + path);
}

static bool backup_corrupt_file(const std::string& path, std::vector<std::string>* errors)
{
    if (!fs::exists(path))
        return false;
    const std::string backup = path + ".corrupt";
    try {
        fs::remove(backup);
        fs::rename(path, backup);
        if (errors)
            errors->push_back("Renamed corrupt file to " + backup);
        return true;
    } catch (const std::exception& e) {
        if (errors)
            errors->push_back(std::string("Could not backup corrupt file: ") + e.what());
        return false;
    }
}

FailureDatabase& FailureDatabase::instance()
{
    static FailureDatabase db;
    return db;
}

std::string FailureDatabase::storage_path() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_storage_path.empty())
        return m_storage_path;
    return smart_print_data_dir();
}

void FailureDatabase::load(std::vector<std::string>* load_errors)
{
    ensure_smart_print_storage_ready();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_had_load_error = false;
    m_load_errors.clear();
    m_storage_path = smart_print_data_dir();
    fs::create_directories(m_storage_path);
    const std::string path = m_storage_path + "/history.json";
    if (!fs::exists(path))
        return;

    try {
        boost::nowide::ifstream ifs(path);
        json root;
        ifs >> root;
        m_records.clear();
        m_success_printer_ids.clear();
        m_success_timestamps.clear();
        m_success_gcode_files.clear();
        m_success_printer_names.clear();
        m_success_subtask_names.clear();
        m_success_config_json.clear();

        if (root.contains("failures")) {
            for (const auto& item : root["failures"]) {
                PrintFailureRecord r;
                r.record_id           = item.value("record_id", "");
                r.timestamp_utc_ms    = item.value("timestamp_utc_ms", (int64_t)0);
                r.printer_id          = item.value("printer_id", "");
                r.printer_name        = item.value("printer_name", "");
                r.printer_model       = item.value("printer_model", "");
                r.gcode_file          = item.value("gcode_file", "");
                r.subtask_name        = item.value("subtask_name", "");
                r.job_id              = item.value("job_id", "");
                r.mc_print_error_code = item.value("mc_print_error_code", 0);
                r.print_error         = item.value("print_error", 0);
                r.mc_print_stage      = item.value("mc_print_stage", 0);
                r.mc_print_percent    = item.value("mc_print_percent", 0);
                r.nozzle_temp         = item.value("nozzle_temp", 0.f);
                r.bed_temp            = item.value("bed_temp", 0.f);
                r.chamber_temp        = item.value("chamber_temp", 0.f);
                r.print_status        = item.value("print_status", "");
                r.user_feedback       = item.value("user_feedback", "");
                r.config_snapshot_verified = item.value("config_snapshot_verified", false);
                r.plate_index              = item.value("plate_index", -1);
                r.config_snapshot_hash     = item.value("config_snapshot_hash", "");
                r.failure_log_bundle_dir   = item.value("failure_log_bundle_dir", "");
                if (item.contains("config_snapshot")) {
                    try {
                        r.config_snapshot = ConfigSnapshot::from_json(item["config_snapshot"].get<std::string>());
                    } catch (...) {
                        m_had_load_error = true;
                        m_load_errors.push_back("Skipped invalid config snapshot in record " + r.record_id);
                    }
                }
                if (item.contains("hms_codes"))
                    for (const auto& h : item["hms_codes"])
                        r.hms_codes.push_back(h.get<std::string>());
                if (item.contains("diagnosis"))
                    r.diagnosis = diagnosis_from_json(item["diagnosis"]);
                m_records.push_back(std::move(r));
            }
        }

        if (root.contains("successes")) {
            for (const auto& s : root["successes"]) {
                m_success_printer_ids.push_back(s.value("printer_id", ""));
                m_success_timestamps.push_back(s.value("timestamp_utc_ms", (int64_t)0));
                m_success_gcode_files.push_back(s.value("gcode_file", ""));
                m_success_printer_names.push_back(s.value("printer_name", ""));
                m_success_subtask_names.push_back(s.value("subtask_name", ""));
                m_success_config_json.push_back(s.value("config_snapshot", ""));
            }
        }
    } catch (const std::exception& e) {
        m_had_load_error = true;
        const std::string msg = std::string("BambuSmartPrint: failed to load history.json: ") + e.what();
        m_load_errors.push_back(msg);
        BOOST_LOG_TRIVIAL(error) << msg;
        backup_corrupt_file(path, load_errors ? load_errors : &m_load_errors);
        m_records.clear();
        m_success_printer_ids.clear();
        m_success_timestamps.clear();
        m_success_gcode_files.clear();
        m_success_printer_names.clear();
        m_success_subtask_names.clear();
        m_success_config_json.clear();
    }

    if (load_errors && !m_load_errors.empty())
        load_errors->insert(load_errors->end(), m_load_errors.begin(), m_load_errors.end());
}

bool FailureDatabase::save()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_last_save_error.clear();
    if (m_storage_path.empty())
        m_storage_path = smart_print_data_dir();
    fs::create_directories(m_storage_path);
    const std::string path = m_storage_path + "/history.json";

    json root;
    json failures = json::array();
    for (const PrintFailureRecord& r : m_records) {
        json item;
        item["record_id"]            = r.record_id;
        item["timestamp_utc_ms"]     = r.timestamp_utc_ms;
        item["printer_id"]           = r.printer_id;
        item["printer_name"]         = r.printer_name;
        item["printer_model"]        = r.printer_model;
        item["gcode_file"]           = r.gcode_file;
        item["subtask_name"]         = r.subtask_name;
        item["job_id"]               = r.job_id;
        item["mc_print_error_code"]  = r.mc_print_error_code;
        item["print_error"]          = r.print_error;
        item["mc_print_stage"]       = r.mc_print_stage;
        item["mc_print_percent"]     = r.mc_print_percent;
        item["nozzle_temp"]          = r.nozzle_temp;
        item["bed_temp"]             = r.bed_temp;
        item["chamber_temp"]         = r.chamber_temp;
        item["print_status"]         = r.print_status;
        item["user_feedback"]        = r.user_feedback;
        item["config_snapshot_verified"] = r.config_snapshot_verified;
        item["plate_index"]              = r.plate_index;
        item["config_snapshot_hash"]     = r.config_snapshot_hash;
        item["failure_log_bundle_dir"]   = r.failure_log_bundle_dir;
        item["config_snapshot"]      = ConfigSnapshot::to_json(r.config_snapshot);
        item["hms_codes"]            = r.hms_codes;
        if (!r.diagnosis.title.empty() || r.diagnosis.category != FailureCategory::Unknown)
            item["diagnosis"] = diagnosis_to_json(r.diagnosis);
        failures.push_back(item);
    }
    root["failures"] = failures;

    json successes = json::array();
    for (size_t i = 0; i < m_success_printer_ids.size(); ++i) {
        json s;
        s["printer_id"]       = m_success_printer_ids[i];
        s["timestamp_utc_ms"] = i < m_success_timestamps.size() ? m_success_timestamps[i] : 0;
        s["gcode_file"]       = i < m_success_gcode_files.size() ? m_success_gcode_files[i] : "";
        s["printer_name"]     = i < m_success_printer_names.size() ? m_success_printer_names[i] : "";
        s["subtask_name"]     = i < m_success_subtask_names.size() ? m_success_subtask_names[i] : "";
        s["config_snapshot"]  = i < m_success_config_json.size() ? m_success_config_json[i] : "";
        successes.push_back(s);
    }
    root["successes"] = successes;

    try {
        atomic_write_json(path, root);
        return true;
    } catch (const std::exception& e) {
        m_last_save_error = std::string("history.json: ") + e.what();
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint: failed to save history.json: " << e.what();
        return false;
    }
}

void FailureDatabase::clear_all_data()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_records.clear();
        m_success_printer_ids.clear();
        m_success_timestamps.clear();
        m_success_gcode_files.clear();
        m_success_printer_names.clear();
        m_success_subtask_names.clear();
        m_success_config_json.clear();
    }
    save();
}

void FailureDatabase::set_record_feedback(const std::string& record_id, const std::string& feedback)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& r : m_records) {
            if (r.record_id == record_id) {
                r.user_feedback = feedback;
                break;
            }
        }
    }
    save();
}

void FailureDatabase::append(const PrintFailureRecord& record)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_records.push_back(record);
        if (m_records.size() > 500)
            m_records.erase(m_records.begin(), m_records.begin() + (m_records.size() - 500));
    }
    save();
}

void FailureDatabase::append_success(const std::string& printer_id, const std::string& gcode_file,
                                     const DynamicPrintConfig& config,
                                     const std::string& printer_name, const std::string& subtask_name)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_success_printer_ids.push_back(printer_id);
        m_success_timestamps.push_back(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        m_success_gcode_files.push_back(gcode_file);
        m_success_printer_names.push_back(printer_name);
        m_success_subtask_names.push_back(subtask_name);
        m_success_config_json.push_back(ConfigSnapshot::to_json(config));
        const size_t max_success = 1000;
        if (m_success_printer_ids.size() > max_success) {
            size_t drop = m_success_printer_ids.size() - max_success;
            m_success_printer_ids.erase(m_success_printer_ids.begin(), m_success_printer_ids.begin() + drop);
            m_success_timestamps.erase(m_success_timestamps.begin(), m_success_timestamps.begin() + drop);
            m_success_gcode_files.erase(m_success_gcode_files.begin(), m_success_gcode_files.begin() + drop);
            m_success_printer_names.erase(m_success_printer_names.begin(), m_success_printer_names.begin() + drop);
            m_success_subtask_names.erase(m_success_subtask_names.begin(), m_success_subtask_names.begin() + drop);
            m_success_config_json.erase(m_success_config_json.begin(), m_success_config_json.begin() + drop);
        }
    }
    save();
}

std::vector<PrintFailureRecord> FailureDatabase::records_for_printer(const std::string& printer_id, size_t limit) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PrintFailureRecord> out;
    for (auto it = m_records.rbegin(); it != m_records.rend() && out.size() < limit; ++it) {
        if (it->printer_id == printer_id)
            out.push_back(*it);
    }
    return out;
}

std::vector<PrintFailureRecord> FailureDatabase::all_records(size_t limit) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_records.empty())
        return {};
    size_t n = std::min(limit, m_records.size());
    return std::vector<PrintFailureRecord>(m_records.end() - n, m_records.end());
}

bool FailureDatabase::find_record(const std::string& record_id, PrintFailureRecord* out) const
{
    if (record_id.empty() || !out)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_records.rbegin(); it != m_records.rend(); ++it) {
        if (it->record_id == record_id) {
            *out = *it;
            return true;
        }
    }
    return false;
}

static bool record_counts_as_failure(const PrintFailureRecord& r)
{
    if (r.diagnosis.category == FailureCategory::UserCancelled)
        return false;
    if (r.print_status == "CANCELLED" || r.print_status == "CANCEL")
        return false;
    return true;
}

int FailureDatabase::count_failures(const std::string& printer_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int c = 0;
    for (const auto& r : m_records)
        if (r.printer_id == printer_id && record_counts_as_failure(r))
            ++c;
    return c;
}

int FailureDatabase::count_all_failures() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int c = 0;
    for (const auto& r : m_records)
        if (record_counts_as_failure(r))
            ++c;
    return c;
}

int FailureDatabase::count_failures_recent(const std::string& printer_id, int64_t window_ms) const
{
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t cutoff = now - window_ms;
    std::lock_guard<std::mutex> lock(m_mutex);
    int c = 0;
    for (const auto& r : m_records)
        if (r.printer_id == printer_id && r.timestamp_utc_ms >= cutoff && record_counts_as_failure(r))
            ++c;
    return c;
}

namespace {

static std::string failure_category_key(FailureCategory c)
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

} // namespace

std::map<std::string, int> FailureDatabase::count_failures_recent_by_category(const std::string& printer_id,
                                                                              int64_t window_ms) const
{
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t cutoff = now - window_ms;
    std::map<std::string, int> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& r : m_records) {
        if (r.printer_id != printer_id || r.timestamp_utc_ms < cutoff || !record_counts_as_failure(r))
            continue;
        ++out[failure_category_key(r.diagnosis.category)];
    }
    return out;
}

int FailureDatabase::count_all_failures_recent(int64_t window_ms) const
{
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t cutoff = now - window_ms;
    std::lock_guard<std::mutex> lock(m_mutex);
    int c = 0;
    for (const auto& r : m_records)
        if (r.timestamp_utc_ms >= cutoff && record_counts_as_failure(r))
            ++c;
    return c;
}

int FailureDatabase::count_successes(const std::string& printer_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int c = 0;
    for (const auto& id : m_success_printer_ids)
        if (id == printer_id) ++c;
    return c;
}

int FailureDatabase::count_all_successes() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_success_printer_ids.size());
}

std::vector<FailureDatabase::SuccessRecord> FailureDatabase::success_records(size_t limit) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SuccessRecord> out;
    const size_t n = m_success_printer_ids.size();
    if (n == 0)
        return out;
    const size_t start = n > limit ? n - limit : 0;
    for (size_t i = start; i < n; ++i) {
        SuccessRecord r;
        r.printer_id       = m_success_printer_ids[i];
        r.timestamp_utc_ms = i < m_success_timestamps.size() ? m_success_timestamps[i] : 0;
        r.gcode_file       = i < m_success_gcode_files.size() ? m_success_gcode_files[i] : "";
        r.printer_name     = i < m_success_printer_names.size() ? m_success_printer_names[i] : "";
        r.subtask_name     = i < m_success_subtask_names.size() ? m_success_subtask_names[i] : "";
        r.config_snapshot_json = i < m_success_config_json.size() ? m_success_config_json[i] : "";
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
