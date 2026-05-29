#include "ConfigVersionStack.hpp"
#include "BambuSmartPrintPaths.hpp"
#include "ConfigSnapshot.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>
#include <chrono>

namespace fs = boost::filesystem;

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

std::string stack_path()
{
    return smart_print_data_dir() + "/config_version_stack.json";
}

} // namespace

ConfigVersionStack& ConfigVersionStack::instance()
{
    static ConfigVersionStack s;
    return s;
}

void ConfigVersionStack::load()
{
    m_stack.clear();
    const std::string path = stack_path();
    if (!fs::exists(path)) return;
    try {
        boost::nowide::ifstream ifs(path);
        nlohmann::json j;
        ifs >> j;
        if (!j.is_array()) return;
        for (const auto& item : j) {
            ConfigVersionEntry e;
            e.id = item.value("id", "");
            e.timestamp_utc_ms = item.value("timestamp_utc_ms", int64_t(0));
            e.label = item.value("label", "");
            e.printer_id = item.value("printer_id", "");
            e.plate_index = item.value("plate_index", -1);
            if (item.contains("config_json"))
                e.config = ConfigSnapshot::from_json_safe(item["config_json"].get<std::string>());
            if (!e.id.empty())
                m_stack.push_back(std::move(e));
        }
    } catch (...) {
        m_stack.clear();
    }
}

bool ConfigVersionStack::save()
{
    ensure_smart_print_storage_ready();
    nlohmann::json arr = nlohmann::json::array();
    for (const ConfigVersionEntry& e : m_stack) {
        nlohmann::json item;
        item["id"] = e.id;
        item["timestamp_utc_ms"] = e.timestamp_utc_ms;
        item["label"] = e.label;
        item["printer_id"] = e.printer_id;
        item["plate_index"] = e.plate_index;
        item["config_json"] = ConfigSnapshot::to_json(e.config);
        arr.push_back(std::move(item));
    }
    return atomic_write_text_file(stack_path(), arr.dump(2));
}

void ConfigVersionStack::push(const std::string& label, const std::string& printer_id, int plate_index,
                            const DynamicPrintConfig& config)
{
    const std::string fingerprint = ConfigSnapshot::fingerprint(config);
    if (!m_stack.empty()) {
        const ConfigVersionEntry& top = m_stack.back();
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (top.label == label && top.printer_id == printer_id && top.plate_index == plate_index
            && now - top.timestamp_utc_ms < 5000
            && ConfigSnapshot::fingerprint(top.config) == fingerprint)
            return;
    }

    ConfigVersionEntry e;
    e.timestamp_utc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    e.id = std::to_string(e.timestamp_utc_ms);
    e.label = label;
    e.printer_id = printer_id;
    e.plate_index = plate_index;
    e.config = config;
    m_stack.push_back(std::move(e));
    while (m_stack.size() > kMaxEntries)
        m_stack.pop_front();
    save();
}

bool ConfigVersionStack::pop(ConfigVersionEntry* out)
{
    if (m_stack.empty()) return false;
    if (out) *out = m_stack.back();
    m_stack.pop_back();
    save();
    return true;
}

bool ConfigVersionStack::peek(ConfigVersionEntry* out) const
{
    if (m_stack.empty() || !out) return false;
    *out = m_stack.back();
    return true;
}

bool ConfigVersionStack::restore_previous(DynamicPrintConfig* out_config, ConfigVersionEntry* meta)
{
    if (!pop(meta) || !out_config) return false;
    *out_config = meta->config;
    return true;
}

size_t ConfigVersionStack::size() const { return m_stack.size(); }

void ConfigVersionStack::clear()
{
    m_stack.clear();
    save();
}

} // namespace BambuSmartPrint
} // namespace Slic3r
