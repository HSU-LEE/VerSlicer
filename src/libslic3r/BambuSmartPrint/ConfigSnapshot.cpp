#include "ConfigSnapshot.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "nlohmann/json.hpp"

#include <set>
#include <functional>

namespace Slic3r {
namespace BambuSmartPrint {

using json = nlohmann::json;

namespace {

// serialize() on some enum options can AV when keys_map is null or the value is out of range.
std::string safe_option_serialize(const ConfigOption* opt)
{
    if (opt == nullptr)
        return "";

    if (opt->type() == coEnum) {
        if (const auto* eg = dynamic_cast<const ConfigOptionEnumGeneric*>(opt)) {
            if (eg->keys_map == nullptr)
                return std::to_string(eg->value);
        } else if (const auto* ev = dynamic_cast<const ConfigOptionEnumsGeneric*>(opt)) {
            if (ev->keys_map == nullptr)
                return {};
        }
    }

    return opt->serialize();
}

bool options_effectively_equal(const ConfigOption* a, const ConfigOption* b, std::string& sa, std::string& sb)
{
    if (a == nullptr && b == nullptr) {
        sa.clear();
        sb.clear();
        return true;
    }
    if (a == nullptr || b == nullptr) {
        sa = safe_option_serialize(a);
        sb = safe_option_serialize(b);
        return sa == sb;
    }
    if (a->type() == coEnum && b->type() == coEnum && a->is_scalar() && b->is_scalar())
        return a->getInt() == b->getInt();
    sa = safe_option_serialize(a);
    sb = safe_option_serialize(b);
    return sa == sb;
}

} // namespace

std::string ConfigSnapshot::to_json(const DynamicPrintConfig& config)
{
    json j;
    for (const std::string& opt_key : config.keys()) {
        const ConfigOption* opt = config.option(opt_key);
        if (opt == nullptr)
            continue;
        j[opt_key] = safe_option_serialize(opt);
    }
    return j.dump();
}

DynamicPrintConfig ConfigSnapshot::from_json_safe(const std::string& json_str)
{
    try {
        return from_json(json_str);
    } catch (...) {
        return DynamicPrintConfig();
    }
}

DynamicPrintConfig ConfigSnapshot::from_json(const std::string& json_str)
{
    DynamicPrintConfig config;
    if (json_str.empty())
        return config;
    json j = json::parse(json_str);
    for (auto it = j.begin(); it != j.end(); ++it) {
        const ConfigOptionDef* def = config.def()->get(it.key());
        if (def == nullptr)
            continue;
        config.set_deserialize_strict(it.key(), it.value().get<std::string>());
    }
    return config;
}

std::vector<SettingChange> ConfigSnapshot::diff(const DynamicPrintConfig& before, const DynamicPrintConfig& after)
{
    std::vector<SettingChange> changes;
    std::set<std::string> keys;
    for (const std::string& k : before.keys()) keys.insert(k);
    for (const std::string& k : after.keys()) keys.insert(k);

    for (const std::string& key : keys) {
        const ConfigOption* a = before.option(key);
        const ConfigOption* b = after.option(key);
        std::string sa;
        std::string sb;
        if (!options_effectively_equal(a, b, sa, sb)) {
            SettingChange c;
            c.key        = key;
            c.old_value  = sa;
            c.new_value  = sb;
            changes.push_back(std::move(c));
        }
    }
    return changes;
}

std::string ConfigSnapshot::fingerprint(const DynamicPrintConfig& config)
{
    const std::string blob = to_json(config);
    const size_t h = std::hash<std::string>{}(blob);
    return std::to_string(h);
}

} // namespace BambuSmartPrint
} // namespace Slic3r
