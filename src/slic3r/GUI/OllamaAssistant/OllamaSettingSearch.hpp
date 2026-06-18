#ifndef slic3r_OllamaSettingSearch_hpp_
#define slic3r_OllamaSettingSearch_hpp_

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace Slic3r { class DynamicPrintConfig; }

namespace Slic3r { namespace GUI {

struct OllamaSettingSearchHit
{
    std::string key;
    int         score{0};
};

/** Fuzzy search + lookup over auto-generated setting catalog. */
class OllamaSettingSearch
{
public:
    static std::vector<OllamaSettingSearchHit> search(const std::string& query, int max_tier = 2,
                                                      size_t limit = 15);
    /** Merge fuzzy search + symptom-derived keys (deduped, ranked). */
    static std::vector<std::string>              candidate_keys_for_request(const std::string& query,
                                                                            int max_tier = 2,
                                                                            size_t limit = 8);
    static nlohmann::json                        lookup(const std::vector<std::string>& keys,
                                                        const DynamicPrintConfig* print_cfg,
                                                        const DynamicPrintConfig* filament_cfg, bool ko_ui);
    static std::vector<std::string>              keys_from_planner_json(const std::string& planner_text);
};

}} // namespace

#endif
