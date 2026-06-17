#ifndef slic3r_BambuSmartPrintTypes_hpp_
#define slic3r_BambuSmartPrintTypes_hpp_

#include <string>
#include <vector>
#include <map>
#include <cstdint>

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

enum class PrintOutcome : int {
    Unknown = 0,
    Success,
    Failed,
};

enum class FailureCategory : int {
    Unknown = 0,
    Adhesion,
    Filament,
    Temperature,
    Mechanical,
    Gcode,
    Network,
    UserCancelled,
};

enum class RiskSeverity : int {
    Info = 0,
    Low,
    Medium,
    High,
};

enum class ReadinessTier : int {
    Risky = 0,
    Fair,
    Good,
    Excellent,
};

enum class PredictionConfidence : int {
    Low = 0,
    Medium,
    High,
};

struct PendingLearningItem {
    std::string item_id;
    std::string category_key;
    std::string summary;
    int64_t     created_ms{ 0 };
};

struct ModelAnalysis {
    double volume_mm3{ 0.0 };
    double height_mm{ 0.0 };
    double max_xy_mm{ 0.0 };
    double overhang_ratio{ 0.0 };       // 0..1 (mesh face ratio)
    double overhang_face_ratio{ 0.0 };  // 0..1 area-weighted steep faces
    double max_overhang_angle_deg{ 0.0 };
    double min_xy_footprint_mm2{ 0.0 };
    double first_layer_contact_ratio{ 0.0 }; // 0..1
    bool   needs_brim{ false };
    bool   tall_narrow{ false };
    double aspect_ratio{ 0.0 };       // height / max_xy
    int    complexity_score{ 0 };     // 0..100 heuristic
    bool   is_small_part{ false };
    bool   thin_feature_risk{ false };
    bool   fits_bed{ true };
    double bed_scale_factor{ 1.0 }; // 1.0 = fits; lower means scale down needed
    std::string suggested_orientation_hint;
    std::string suggested_material; // PLA, PETG, ABS, TPU
};

struct PrintInsight {
    std::string label;
    std::string detail;
    RiskSeverity severity{ RiskSeverity::Info };
};

struct ReadinessReport {
    float score{ 0.f };           // 0..100
    float success_rate{ 0.f };   // mirrors prediction
    ReadinessTier tier{ ReadinessTier::Fair };
    std::string headline;
    std::vector<PrintInsight> insights;
    std::vector<std::string> action_items;
    bool filament_mismatch{ false };
    std::string active_filament_hint;
    std::string suggested_filament_hint;
};

struct PlateBatchEntry {
    int         plate_index{ 0 }; // 0-based
    float       readiness_score{ 0.f };
    float       priority_score{ 0.f };
    size_t      change_count{ 0 };
    std::string suggested_material;
    std::string active_filament_family;
    int         complexity_score{ 0 };
    bool        empty{ true };
};

struct PlateBatchSummary {
    std::vector<PlateBatchEntry> plates;
    int  plates_with_models{ 0 };
    int  best_plate_index{ -1 };      // most changes or lowest readiness
    int  highest_readiness_plate{ -1 };
    int  lowest_readiness_plate{ -1 };
    float average_readiness{ 0.f };
    size_t total_suggested_changes{ 0 };
    std::string filament_conflict_note;
};

struct SliceAnalysis {
    bool  valid{ false };
    float overhang_area_ratio{ 0.f }; // 0..1
    int   unsupported_islands_count{ 0 };
    float bridge_length_max_mm{ 0.f };
    std::vector<std::string> risk_notes;
};

struct SettingChange {
    std::string key;
    std::string old_value;
    std::string new_value;
    std::string reason;
};

struct AutoSettingsResult {
    DynamicPrintConfig config_delta;
    std::vector<SettingChange> changes;
    std::vector<SettingChange> blocked_changes;
    std::string summary;
};

struct FailureDiagnosis {
    FailureCategory category{ FailureCategory::Unknown };
    std::string title;
    std::string description;
    std::string action_line;
    std::vector<std::string> likely_causes;
    std::vector<SettingChange> recommended_fixes;
    float confidence{ 0.0f }; // 0..1
};

struct SuccessPrediction {
    float success_rate{ 0.0f }; // 0..100
    std::string summary;
    std::vector<std::string> risk_factors;
    PredictionConfidence confidence{ PredictionConfidence::Low };
};

struct PrintFailureRecord {
    std::string record_id;
    int64_t     timestamp_utc_ms{ 0 };
    std::string printer_id;
    std::string printer_name;
    std::string printer_model;
    std::string gcode_file;
    std::string subtask_name;
    std::string job_id;
    int         mc_print_error_code{ 0 };
    int         print_error{ 0 };
    int         mc_print_stage{ 0 };
    int         mc_print_percent{ 0 };
    float       nozzle_temp{ 0.f };
    float       bed_temp{ 0.f };
    float       chamber_temp{ 0.f };
    std::string print_status;
    std::vector<std::string> hms_codes;
    DynamicPrintConfig config_snapshot;
    bool        config_snapshot_verified{ false };
    int         plate_index{ -1 };
    std::string config_snapshot_hash;
    FailureDiagnosis diagnosis;
    std::string user_feedback; // "helpful" | "not_helpful" | ""
    std::string failure_log_bundle_dir; // auto-captured artifacts on failure
};

struct PrinterLearningProfile {
    std::string printer_id;
    int total_prints{ 0 };
    int successful_prints{ 0 };
    int failed_prints{ 0 };
    std::map<std::string, int> failures_by_category;
    std::map<std::string, float> setting_adjustments; // key -> bias offset
    std::map<std::string, bool> category_bias_paused;   // category_key -> skip bias bumps
    std::map<std::string, int> setting_attribution_positive;
    std::map<std::string, int> setting_attribution_negative;
    std::vector<std::string> last_applied_setting_keys;
    std::vector<PendingLearningItem> pending_learning;
    int applied_learning_count{ 0 };
    int helpful_learning_count{ 0 };
    int64_t last_failure_ms{ 0 };
    int64_t last_success_ms{ 0 };
    int64_t last_bias_decay_ms{ 0 };
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
