#ifndef slic3r_OllamaSettingSearch_hpp_
#define slic3r_OllamaSettingSearch_hpp_

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Slic3r { class DynamicPrintConfig; }

namespace Slic3r { namespace GUI {

struct OllamaSettingSearchHit
{
    std::string key;
    int         score{0};
};

/** One catalog setting the user's words clearly turn on or off. */
struct OllamaObviousSettingChange
{
    std::string    preset;
    std::string    key;
    std::string    label;
    nlohmann::json value;
};

/** Fuzzy search + lookup over auto-generated setting catalog. */
class OllamaSettingSearch
{
public:
    static std::vector<OllamaSettingSearchHit> search(const std::string& query, int max_tier = 2,
                                                      size_t limit = 15);
    /** Fuzzy catalog search ranked by relevance. */
    static std::vector<std::string>              candidate_keys_for_request(const std::string& query,
                                                                            int max_tier = 2,
                                                                            size_t limit = 8);
    static nlohmann::json                        lookup(const std::vector<std::string>& keys,
                                                        const DynamicPrintConfig* print_cfg,
                                                        const DynamicPrintConfig* filament_cfg, bool ko_ui);
    static std::vector<std::string>              keys_from_planner_json(const std::string& planner_text);

    /**
     * A single on/off match, such as "다림질 켜줘" → ironing_type=top.
     * Empty when several settings match or the words are not on/off.
     */
    static std::optional<OllamaObviousSettingChange> obvious_toggle(const std::string& user_goal);
};

}} // namespace

#endif
