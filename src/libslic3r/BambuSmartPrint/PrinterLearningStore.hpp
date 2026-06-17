#ifndef slic3r_PrinterLearningStore_hpp_
#define slic3r_PrinterLearningStore_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <mutex>
#include <map>
#include <vector>
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

class PrinterLearningStore
{
public:
    static PrinterLearningStore& instance();

    void load(std::vector<std::string>* load_errors = nullptr);
    bool save();
    const std::string& last_save_error() const { return m_last_save_error; }

    bool had_load_error() const { return m_had_load_error; }
    const std::vector<std::string>& load_error_messages() const { return m_load_errors; }

    PrinterLearningProfile get_profile(const std::string& printer_id) const;
    void record_failure(const std::string& printer_id, FailureCategory category, const DynamicPrintConfig& config,
                        bool apply_bias_immediately = true);
    void record_success(const std::string& printer_id);
    void record_diagnosis_feedback(const std::string& printer_id, FailureCategory category, const std::string& feedback);
    void record_applied_suggestion(const std::string& printer_id);
    void record_applied_suggestion(const std::string& printer_id, const std::vector<std::string>& setting_keys);

    bool approve_pending_learning(const std::string& printer_id, const std::string& item_id);
    bool dismiss_pending_learning(const std::string& printer_id, const std::string& item_id);

    void apply_learning_to_config(const std::string& printer_id, DynamicPrintConfig& config) const;
    void clear_all_data();
    size_t profile_count() const;

private:
    PrinterLearningStore() = default;
    void apply_category_bias(PrinterLearningProfile& p, FailureCategory category, const DynamicPrintConfig& config);
    void enqueue_pending(PrinterLearningProfile& p, FailureCategory category);

    mutable std::mutex m_mutex;
    std::map<std::string, PrinterLearningProfile> m_profiles;
    std::string m_storage_path;
    bool m_had_load_error{ false };
    std::vector<std::string> m_load_errors;
    std::string m_last_save_error;
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
