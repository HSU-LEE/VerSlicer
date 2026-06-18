#ifndef slic3r_OllamaSettingRegistry_hpp_
#define slic3r_OllamaSettingRegistry_hpp_

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace Slic3r { class DynamicPrintConfig; }

namespace Slic3r { namespace GUI {

struct OllamaSettingSpec
{
    const char* key;
    const char* value_type;
    const char* unit;
    double      min_v;
    double      max_v;
    const char* desc_en;
    const char* desc_ko;
    const char* format;
    const char* aliases;
    const char* preset_scope{"print"};
    int         context_priority{50};
    bool        virtual_key{false};
};

/** Single source of truth for AI-assistant set_config keys. */
class OllamaSettingRegistry
{
public:
    static const std::vector<OllamaSettingSpec>& all();
    static const OllamaSettingSpec*               find_spec(const std::string& key);
    static bool is_allowed_key(const std::string& key);
    static bool is_allowed_key(const std::string& key, const std::string& preset);
    static bool is_virtual_key(const std::string& key);
    static bool clamp_json_value(const std::string& key, nlohmann::json& value);
    static nlohmann::json build_catalog(const DynamicPrintConfig* cfg, bool ko_ui);
    static nlohmann::json build_priority_catalog(const DynamicPrintConfig* cfg, bool ko_ui, size_t max_entries = 0);
    static nlohmann::json build_setting_index(int max_tier = 2);
    static nlohmann::json lookup_catalog_keys(const DynamicPrintConfig* cfg, bool ko_ui,
                                              const std::vector<std::string>& keys);
    static nlohmann::json allowed_keys_json();
    static std::string  config_fingerprint(const DynamicPrintConfig* cfg);
};

}} // namespace

#endif
