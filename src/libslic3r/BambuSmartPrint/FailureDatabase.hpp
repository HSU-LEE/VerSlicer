#ifndef slic3r_FailureDatabase_hpp_
#define slic3r_FailureDatabase_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <map>
#include <vector>
#include <string>
#include <mutex>

namespace Slic3r {
namespace BambuSmartPrint {

class FailureDatabase
{
public:
    static FailureDatabase& instance();

    void load(std::vector<std::string>* load_errors = nullptr);
    bool save();
    const std::string& last_save_error() const { return m_last_save_error; }

    bool had_load_error() const { return m_had_load_error; }
    const std::vector<std::string>& load_error_messages() const { return m_load_errors; }

    void clear_all_data();
    std::string storage_path() const;

    void append(const PrintFailureRecord& record);
    void append_success(const std::string& printer_id, const std::string& gcode_file, const DynamicPrintConfig& config,
                        const std::string& printer_name = {}, const std::string& subtask_name = {});
    void set_record_feedback(const std::string& record_id, const std::string& feedback);

    std::vector<PrintFailureRecord> records_for_printer(const std::string& printer_id, size_t limit = 50) const;
    std::vector<PrintFailureRecord> all_records(size_t limit = 200) const;
    bool find_record(const std::string& record_id, PrintFailureRecord* out) const;

    int count_failures(const std::string& printer_id) const;
    int count_all_failures() const;
    int count_failures_recent(const std::string& printer_id, int64_t window_ms = 30LL * 24 * 3600 * 1000) const;
    std::map<std::string, int> count_failures_recent_by_category(const std::string& printer_id,
        int64_t window_ms = 30LL * 24 * 3600 * 1000) const;
    int count_all_failures_recent(int64_t window_ms = 30LL * 24 * 3600 * 1000) const;
    int count_successes(const std::string& printer_id) const;
    int count_all_successes() const;

    struct SuccessRecord {
        int64_t     timestamp_utc_ms{ 0 };
        std::string printer_id;
        std::string printer_name;
        std::string gcode_file;
        std::string subtask_name;
        std::string config_snapshot_json;
    };
    std::vector<SuccessRecord> success_records(size_t limit = 200) const;

private:
    FailureDatabase() = default;
    mutable std::mutex m_mutex;
    std::vector<PrintFailureRecord> m_records;
    std::vector<std::string>        m_success_printer_ids;
    std::vector<int64_t>            m_success_timestamps;
    std::vector<std::string>        m_success_gcode_files;
    std::vector<std::string>        m_success_printer_names;
    std::vector<std::string>        m_success_subtask_names;
    std::vector<std::string>        m_success_config_json;
    std::string                     m_storage_path;
    bool                            m_had_load_error{ false };
    std::vector<std::string>        m_load_errors;
    std::string                     m_last_save_error;
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
