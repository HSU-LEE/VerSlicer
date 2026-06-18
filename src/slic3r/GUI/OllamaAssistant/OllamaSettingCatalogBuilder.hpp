#ifndef slic3r_OllamaSettingCatalogBuilder_hpp_
#define slic3r_OllamaSettingCatalogBuilder_hpp_

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace Slic3r { class DynamicPrintConfig; }

namespace Slic3r { namespace GUI {

enum class OllamaSettingTier : int
{
    Simple     = 1,
    Advanced   = 2,
    Restricted = 3,
};

struct OllamaAutoSettingSpec
{
    std::string       key;
    std::string       preset_scope;
    std::string       value_type;
    std::string       unit;
    double            min_v{0.0};
    double            max_v{0.0};
    std::string       label;
    std::string       category;
    std::string       format;
    std::string       tooltip;
    OllamaSettingTier tier{OllamaSettingTier::Advanced};
    int               context_priority{50};
    bool              virtual_key{false};
};

/** Builds AI setting metadata from PrintConfigDef + Preset option lists. */
class OllamaSettingCatalogBuilder
{
public:
    static const std::vector<OllamaAutoSettingSpec>& all();
    static const OllamaAutoSettingSpec*              find(const std::string& key);
    static bool                                      is_restricted_key(const std::string& key);
    static OllamaSettingTier                         tier_for_key(const std::string& key);

    static nlohmann::json build_index(int max_tier = 2);
    static nlohmann::json build_catalog(const DynamicPrintConfig* cfg, bool ko_ui, int max_tier = 2);
    static nlohmann::json build_catalog_for_keys(const DynamicPrintConfig* cfg, bool ko_ui,
                                                 const std::vector<std::string>& keys);
    static nlohmann::json explain_setting(const std::string& key, bool ko_ui);
};

}} // namespace

#endif
