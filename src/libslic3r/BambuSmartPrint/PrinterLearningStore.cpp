#include "PrinterLearningStore.hpp"
#include "BambuSmartPrintPaths.hpp"

#include "nlohmann/json.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <chrono>
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

static std::string category_key(FailureCategory c)
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

static void clamp_adjustments(PrinterLearningProfile& p);
static void decay_adjustments(PrinterLearningProfile& p, float factor);

static void clamp_adjustments(PrinterLearningProfile& p)
{
    auto clamp = [&](const char* key, float lo, float hi) {
        auto it = p.setting_adjustments.find(key);
        if (it != p.setting_adjustments.end())
            it->second = std::max(lo, std::min(hi, it->second));
    };
    clamp("initial_layer_speed", -30.f, 0.f);
    clamp("bed_temperature", 0.f, 15.f);
    clamp("brim_width", 0.f, 10.f);
    clamp("retraction_length", 0.f, 2.f);
    clamp("outer_wall_speed", -40.f, 0.f);
    clamp("fan_min_speed", 0.f, 20.f);
}

static void decay_adjustments(PrinterLearningProfile& p, float factor)
{
    for (auto& kv : p.setting_adjustments)
        kv.second *= factor;
    clamp_adjustments(p);
}

static void maybe_decay_bias(PrinterLearningProfile& p)
{
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    constexpr int64_t kWeekMs = 7LL * 24 * 3600 * 1000;
    if (p.last_bias_decay_ms > 0 && now - p.last_bias_decay_ms < kWeekMs)
        return;
    decay_adjustments(p, 0.95f);
    p.last_bias_decay_ms = now;
}

static void bump_attribution(PrinterLearningProfile& p, const std::vector<std::string>& keys, bool positive)
{
    auto& bucket = positive ? p.setting_attribution_positive : p.setting_attribution_negative;
    for (const std::string& key : keys) {
        if (key.empty())
            continue;
        ++bucket[key];
    }
}

PrinterLearningStore& PrinterLearningStore::instance()
{
    static PrinterLearningStore s;
    return s;
}

static bool backup_corrupt_learning(const std::string& path)
{
    if (!fs::exists(path)) return false;
    try {
        fs::remove(path + ".corrupt");
        fs::rename(path, path + ".corrupt");
        return true;
    } catch (...) {
        return false;
    }
}

void PrinterLearningStore::load(std::vector<std::string>* load_errors)
{
    ensure_smart_print_storage_ready();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_had_load_error = false;
    m_load_errors.clear();
    m_storage_path = smart_print_data_dir();
    fs::create_directories(m_storage_path);
    const std::string path = m_storage_path + "/learning.json";
    if (!fs::exists(path))
        return;

    try {
        boost::nowide::ifstream ifs(path);
        json root;
        ifs >> root;
        m_profiles.clear();
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (it.key() == "_schema_version" || !it.value().is_object())
                continue;
            PrinterLearningProfile p;
            p.printer_id = it.key();
            const json& v = it.value();
            p.total_prints      = v.value("total_prints", 0);
            p.successful_prints = v.value("successful_prints", 0);
            p.failed_prints     = v.value("failed_prints", 0);
            p.last_failure_ms   = v.value("last_failure_ms", (int64_t)0);
            p.last_success_ms   = v.value("last_success_ms", (int64_t)0);
            if (v.contains("failures_by_category"))
                for (auto fc = v["failures_by_category"].begin(); fc != v["failures_by_category"].end(); ++fc)
                    p.failures_by_category[fc.key()] = fc.value().get<int>();
            if (v.contains("setting_adjustments"))
                for (auto sa = v["setting_adjustments"].begin(); sa != v["setting_adjustments"].end(); ++sa)
                    p.setting_adjustments[sa.key()] = sa.value().get<float>();
            if (v.contains("category_bias_paused"))
                for (auto cp = v["category_bias_paused"].begin(); cp != v["category_bias_paused"].end(); ++cp)
                    p.category_bias_paused[cp.key()] = cp.value().get<bool>();
            p.applied_learning_count  = v.value("applied_learning_count", 0);
            p.helpful_learning_count  = v.value("helpful_learning_count", 0);
            p.last_bias_decay_ms      = v.value("last_bias_decay_ms", (int64_t)0);
            if (v.contains("setting_attribution_positive"))
                for (auto sa = v["setting_attribution_positive"].begin(); sa != v["setting_attribution_positive"].end(); ++sa)
                    p.setting_attribution_positive[sa.key()] = sa.value().get<int>();
            if (v.contains("setting_attribution_negative"))
                for (auto sa = v["setting_attribution_negative"].begin(); sa != v["setting_attribution_negative"].end(); ++sa)
                    p.setting_attribution_negative[sa.key()] = sa.value().get<int>();
            if (v.contains("last_applied_setting_keys") && v["last_applied_setting_keys"].is_array())
                for (const auto& k : v["last_applied_setting_keys"])
                    if (k.is_string())
                        p.last_applied_setting_keys.push_back(k.get<std::string>());
            if (v.contains("pending_learning")) {
                for (const auto& item : v["pending_learning"]) {
                    PendingLearningItem pl;
                    pl.item_id      = item.value("item_id", "");
                    pl.category_key = item.value("category_key", "");
                    pl.summary      = item.value("summary", "");
                    pl.created_ms   = item.value("created_ms", (int64_t)0);
                    if (!pl.item_id.empty())
                        p.pending_learning.push_back(std::move(pl));
                }
            }
            clamp_adjustments(p);
            m_profiles[p.printer_id] = std::move(p);
        }
    } catch (const std::exception& e) {
        m_had_load_error = true;
        const std::string msg = std::string("BambuSmartPrint: failed to load learning.json: ") + e.what();
        m_load_errors.push_back(msg);
        BOOST_LOG_TRIVIAL(error) << msg;
        backup_corrupt_learning(path);
        m_profiles.clear();
    }

    if (load_errors && !m_load_errors.empty())
        load_errors->insert(load_errors->end(), m_load_errors.begin(), m_load_errors.end());
}

bool PrinterLearningStore::save()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_last_save_error.clear();
    if (m_storage_path.empty())
        m_storage_path = smart_print_data_dir();
    fs::create_directories(m_storage_path);
    json root;
    root["_schema_version"] = 2;
    for (const auto& kv : m_profiles) {
        const PrinterLearningProfile& p = kv.second;
        json v;
        v["total_prints"]      = p.total_prints;
        v["successful_prints"] = p.successful_prints;
        v["failed_prints"]     = p.failed_prints;
        v["last_failure_ms"]   = p.last_failure_ms;
        v["last_success_ms"]   = p.last_success_ms;
        v["failures_by_category"] = p.failures_by_category;
        v["setting_adjustments"]  = p.setting_adjustments;
        v["category_bias_paused"] = p.category_bias_paused;
        v["applied_learning_count"] = p.applied_learning_count;
        v["helpful_learning_count"] = p.helpful_learning_count;
        v["last_bias_decay_ms"]     = p.last_bias_decay_ms;
        v["setting_attribution_positive"] = p.setting_attribution_positive;
        v["setting_attribution_negative"] = p.setting_attribution_negative;
        v["last_applied_setting_keys"] = p.last_applied_setting_keys;
        json pending = json::array();
        for (const PendingLearningItem& pl : p.pending_learning) {
            pending.push_back({
                { "item_id", pl.item_id },
                { "category_key", pl.category_key },
                { "summary", pl.summary },
                { "created_ms", pl.created_ms }
            });
        }
        v["pending_learning"] = pending;
        root[p.printer_id] = v;
    }
    try {
        atomic_write_json(m_storage_path + "/learning.json", root);
        return true;
    } catch (const std::exception& e) {
        m_last_save_error = std::string("learning.json: ") + e.what();
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint: failed to save learning.json: " << e.what();
        return false;
    }
}

PrinterLearningProfile PrinterLearningStore::get_profile(const std::string& printer_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.find(printer_id);
    if (it != m_profiles.end())
        return it->second;
    PrinterLearningProfile p;
    p.printer_id = printer_id;
    return p;
}

void PrinterLearningStore::apply_category_bias(PrinterLearningProfile& p, FailureCategory category,
                                               const DynamicPrintConfig& config)
{
    const std::string cat = category_key(category);
    const bool paused = p.category_bias_paused.count(cat) && p.category_bias_paused.at(cat);
    if (paused)
        return;

    if (category == FailureCategory::Adhesion) {
        p.setting_adjustments["initial_layer_speed"] -= 5.f;
        p.setting_adjustments["bed_temperature"] += 3.f;
        p.setting_adjustments["brim_width"] = std::max(p.setting_adjustments["brim_width"], 5.f);
    } else if (category == FailureCategory::Filament) {
        p.setting_adjustments["retraction_length"] += 0.5f;
    } else if (category == FailureCategory::Temperature) {
        p.setting_adjustments["bed_temperature"] += 2.f;
    } else if (category == FailureCategory::Mechanical) {
        p.setting_adjustments["outer_wall_speed"] -= 5.f;
    }

    if (config.has("initial_layer_height")) {
        const float h = float(config.opt_float("initial_layer_height"));
        if (h > 0.3f && category == FailureCategory::Adhesion)
            p.setting_adjustments["initial_layer_speed"] -= 2.f;
    }

    clamp_adjustments(p);
}

void PrinterLearningStore::enqueue_pending(PrinterLearningProfile& p, FailureCategory category)
{
    const std::string cat = category_key(category);
    PendingLearningItem item;
    item.category_key = cat;
    item.created_ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    item.item_id = std::to_string(item.created_ms) + "-" + cat;
    switch (category) {
    case FailureCategory::Adhesion:
        item.summary = "Slower first layer, higher bed temp, wider brim";
        break;
    case FailureCategory::Filament:
        item.summary = "Increase retraction slightly";
        break;
    case FailureCategory::Temperature:
        item.summary = "Raise bed temperature bias";
        break;
    case FailureCategory::Mechanical:
        item.summary = "Reduce outer wall speed bias";
        break;
    default:
        item.summary = "Adjust settings based on recent failure category";
        break;
    }
    p.pending_learning.push_back(std::move(item));
    if (p.pending_learning.size() > 20)
        p.pending_learning.erase(p.pending_learning.begin(),
            p.pending_learning.begin() + (p.pending_learning.size() - 20));
}

void PrinterLearningStore::record_failure(const std::string& printer_id, FailureCategory category,
                                          const DynamicPrintConfig& config, bool apply_bias_immediately)
{
    if (category == FailureCategory::UserCancelled)
        return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PrinterLearningProfile& p = m_profiles[printer_id];
        p.printer_id = printer_id;
        maybe_decay_bias(p);
        p.total_prints++;
        p.failed_prints++;
        const std::string cat = category_key(category);
        p.failures_by_category[cat]++;
        p.last_failure_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        bump_attribution(p, p.last_applied_setting_keys, false);

        if (apply_bias_immediately)
            apply_category_bias(p, category, config);
        else
            enqueue_pending(p, category);
    }
    save();
}

bool PrinterLearningStore::approve_pending_learning(const std::string& printer_id, const std::string& item_id)
{
    bool found = false;
    FailureCategory cat = FailureCategory::Unknown;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_profiles.find(printer_id);
        if (it == m_profiles.end())
            return false;
        PrinterLearningProfile& p = it->second;
        for (auto pit = p.pending_learning.begin(); pit != p.pending_learning.end(); ++pit) {
            if (pit->item_id != item_id)
                continue;
            if (pit->category_key == "adhesion") cat = FailureCategory::Adhesion;
            else if (pit->category_key == "filament") cat = FailureCategory::Filament;
            else if (pit->category_key == "temperature") cat = FailureCategory::Temperature;
            else if (pit->category_key == "mechanical") cat = FailureCategory::Mechanical;
            apply_category_bias(p, cat, DynamicPrintConfig());
            p.pending_learning.erase(pit);
            found = true;
            break;
        }
    }
    if (found)
        save();
    return found;
}

bool PrinterLearningStore::dismiss_pending_learning(const std::string& printer_id, const std::string& item_id)
{
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_profiles.find(printer_id);
        if (it == m_profiles.end())
            return false;
        PrinterLearningProfile& p = it->second;
        for (auto pit = p.pending_learning.begin(); pit != p.pending_learning.end(); ++pit) {
            if (pit->item_id != item_id)
                continue;
            p.pending_learning.erase(pit);
            found = true;
            break;
        }
    }
    if (found)
        save();
    return found;
}

void PrinterLearningStore::record_applied_suggestion(const std::string& printer_id)
{
    record_applied_suggestion(printer_id, {});
}

void PrinterLearningStore::record_applied_suggestion(const std::string& printer_id,
                                                       const std::vector<std::string>& setting_keys)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PrinterLearningProfile& p = m_profiles[printer_id];
        p.printer_id = printer_id;
        ++p.applied_learning_count;
        if (!setting_keys.empty())
            p.last_applied_setting_keys = setting_keys;
    }
    save();
}

void PrinterLearningStore::record_diagnosis_feedback(const std::string& printer_id, FailureCategory category,
                                                       const std::string& feedback)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PrinterLearningProfile& p = m_profiles[printer_id];
        p.printer_id = printer_id;
        const std::string cat = category_key(category);
        if (feedback == "not_helpful")
            p.category_bias_paused[cat] = true;
        else if (feedback == "helpful") {
            p.category_bias_paused[cat] = false;
            ++p.helpful_learning_count;
        }
    }
    save();
}

void PrinterLearningStore::clear_all_data()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_profiles.clear();
    }
    save();
}

size_t PrinterLearningStore::profile_count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profiles.size();
}

void PrinterLearningStore::record_success(const std::string& printer_id)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PrinterLearningProfile& p = m_profiles[printer_id];
        p.printer_id = printer_id;
        maybe_decay_bias(p);
        p.total_prints++;
        p.successful_prints++;
        p.last_success_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        bump_attribution(p, p.last_applied_setting_keys, true);
        decay_adjustments(p, 0.9f);
        p.last_applied_setting_keys.clear();
    }
    save();
}

static bool category_paused(const PrinterLearningProfile& p, FailureCategory cat)
{
    const std::string key = category_key(cat);
    auto it = p.category_bias_paused.find(key);
    return it != p.category_bias_paused.end() && it->second;
}

static bool setting_adjustment_paused(const PrinterLearningProfile& p, const std::string& setting_key)
{
    if (setting_key == "initial_layer_speed" || setting_key == "bed_temperature" || setting_key == "brim_width")
        return category_paused(p, FailureCategory::Adhesion);
    if (setting_key == "retraction_length")
        return category_paused(p, FailureCategory::Filament);
    if (setting_key == "outer_wall_speed")
        return category_paused(p, FailureCategory::Mechanical);
    if (setting_key == "fan_min_speed")
        return category_paused(p, FailureCategory::Temperature);
    return false;
}

void PrinterLearningStore::apply_learning_to_config(const std::string& printer_id, DynamicPrintConfig& config) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.find(printer_id);
    if (it == m_profiles.end()) return;
    const PrinterLearningProfile& p = it->second;

    if (!category_paused(p, FailureCategory::Adhesion)
        && p.failures_by_category.count("adhesion") && p.failures_by_category.at("adhesion") >= 2) {
        if (config.has("brim_type"))
            config.set_deserialize_strict("brim_type", "outer_only");
        if (config.has("brim_width")) {
            float w = config.opt_float("brim_width");
            config.set_key_value("brim_width", new ConfigOptionFloat(std::max(w, 5.0f)));
        }
    }
    if (!setting_adjustment_paused(p, "initial_layer_speed")
        && p.setting_adjustments.count("initial_layer_speed") && config.has("initial_layer_speed")) {
        float v = config.opt_float("initial_layer_speed");
        v = std::max(15.f, v + p.setting_adjustments.at("initial_layer_speed"));
        config.set_key_value("initial_layer_speed", new ConfigOptionFloat(v));
    }
    if (!setting_adjustment_paused(p, "bed_temperature")
        && p.setting_adjustments.count("bed_temperature") && config.has("bed_temperature")) {
        float v = config.opt_float("bed_temperature");
        v += p.setting_adjustments.at("bed_temperature");
        config.set_key_value("bed_temperature", new ConfigOptionFloat(v));
    }
    if (!setting_adjustment_paused(p, "retraction_length")
        && p.setting_adjustments.count("retraction_length") && config.has("retraction_length")) {
        float v = config.opt_float("retraction_length");
        v += p.setting_adjustments.at("retraction_length");
        config.set_key_value("retraction_length", new ConfigOptionFloat(std::max(0.f, v)));
    }
    if (!setting_adjustment_paused(p, "outer_wall_speed")
        && p.setting_adjustments.count("outer_wall_speed") && config.has("outer_wall_speed")) {
        float v = config.opt_float("outer_wall_speed");
        v = std::max(20.f, v + p.setting_adjustments.at("outer_wall_speed"));
        config.set_key_value("outer_wall_speed", new ConfigOptionFloat(v));
    }
    if (!setting_adjustment_paused(p, "brim_width")
        && p.setting_adjustments.count("brim_width") && config.has("brim_width")) {
        float w = config.opt_float("brim_width");
        w = std::max(w, p.setting_adjustments.at("brim_width"));
        config.set_key_value("brim_width", new ConfigOptionFloat(w));
    }
    if (!setting_adjustment_paused(p, "fan_min_speed")
        && p.setting_adjustments.count("fan_min_speed") && config.has("fan_min_speed")) {
        float v = config.opt_float("fan_min_speed");
        v += p.setting_adjustments.at("fan_min_speed");
        config.set_key_value("fan_min_speed", new ConfigOptionFloat(std::min(100.f, v)));
    }
}

} // namespace BambuSmartPrint
} // namespace Slic3r
