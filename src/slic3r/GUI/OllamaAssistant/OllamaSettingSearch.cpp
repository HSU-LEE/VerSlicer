#include "OllamaSettingSearch.hpp"

#include "OllamaActionJsonExtract.hpp"
#include "OllamaSettingAliases.hpp"
#include "OllamaSettingCatalogBuilder.hpp"

#include "libslic3r/PrintConfig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Slic3r { namespace GUI {

namespace {

std::string lower_ascii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int score_token(const std::string& haystack, const std::string& token)
{
    if (token.empty())
        return 0;
    if (haystack == token)
        return 100;
    if (haystack.find(token) != std::string::npos)
        return 60;
    return 0;
}

int score_spec(const OllamaAutoSettingSpec& sp, const std::string& query_lower)
{
    int score = score_token(lower_ascii(sp.key), query_lower);
    score     = std::max(score, score_token(lower_ascii(sp.label), query_lower));
    score     = std::max(score, score_token(lower_ascii(sp.category), query_lower));
    score     = std::max(score, score_token(lower_ascii(sp.tooltip), query_lower));
    for (const std::string& ko : OllamaSettingAliases::ko_terms_for_key(sp.key))
        score = std::max(score, score_token(lower_ascii(ko), query_lower));
    score = std::max(score, OllamaSettingAliases::symptom_boost(query_lower, sp.key));
    return score;
}

} // namespace

std::vector<OllamaSettingSearchHit> OllamaSettingSearch::search(const std::string& query, int max_tier,
                                                                 size_t limit)
{
    const std::string q = lower_ascii(query);
    std::vector<OllamaSettingSearchHit> hits;
    if (q.empty())
        return hits;

    for (const OllamaAutoSettingSpec& sp : OllamaSettingCatalogBuilder::all()) {
        if (static_cast<int>(sp.tier) > max_tier || sp.virtual_key)
            continue;
        const int score = score_spec(sp, q);
        if (score <= 0)
            continue;
        hits.push_back({sp.key, score});
    }

    std::sort(hits.begin(), hits.end(), [](const OllamaSettingSearchHit& a, const OllamaSettingSearchHit& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.key < b.key;
    });
    if (hits.size() > limit)
        hits.resize(limit);
    return hits;
}

std::vector<std::string> OllamaSettingSearch::candidate_keys_for_request(const std::string& query, int max_tier,
                                                                           size_t limit)
{
    std::unordered_map<std::string, int> ranked;
    for (const OllamaSettingSearchHit& hit : search(query, max_tier, limit))
        ranked[hit.key] = std::max(ranked[hit.key], hit.score);
    for (const std::string& key : OllamaSettingAliases::keys_from_symptoms(query)) {
        const int boost = OllamaSettingAliases::symptom_boost(lower_ascii(query), key);
        ranked[key]     = std::max(ranked[key], boost > 0 ? boost : 70);
    }
    std::vector<std::pair<int, std::string>> ordered;
    ordered.reserve(ranked.size());
    for (const auto& kv : ranked)
        ordered.push_back({kv.second, kv.first});
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second < b.second;
    });
    std::vector<std::string> keys;
    for (const auto& p : ordered) {
        if (keys.size() >= limit)
            break;
        keys.push_back(p.second);
    }
    return keys;
}

nlohmann::json OllamaSettingSearch::lookup(const std::vector<std::string>& keys,
                                           const DynamicPrintConfig* print_cfg,
                                           const DynamicPrintConfig* filament_cfg, bool ko_ui)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const std::string& key : keys) {
        const OllamaAutoSettingSpec* sp = OllamaSettingCatalogBuilder::find(key);
        if (!sp || sp->virtual_key || sp->tier == OllamaSettingTier::Restricted)
            continue;
        const DynamicPrintConfig* cfg = print_cfg;
        if (sp->preset_scope == "filament")
            cfg = filament_cfg;
        nlohmann::json e;
        e["key"]          = sp->key;
        e["value_type"]   = sp->value_type;
        e["unit"]         = sp->unit;
        e["min"]          = sp->min_v;
        e["max"]          = sp->max_v;
        e["label"]        = sp->label;
        e["category"]     = sp->category;
        e["format"]       = sp->format;
        e["preset_scope"] = sp->preset_scope;
        e["ai_tier"]      = static_cast<int>(sp->tier);
        if (!sp->tooltip.empty())
            e["description"] = sp->tooltip;
        if (cfg && cfg->has(sp->key))
            e["current"] = cfg->opt_serialize(sp->key);
        else
            e["current"] = nullptr;
        (void) ko_ui;
        arr.push_back(std::move(e));
    }
    return arr;
}

std::vector<std::string> OllamaSettingSearch::keys_from_planner_json(const std::string& planner_text)
{
    std::vector<std::string> keys;
    try {
        const nlohmann::json root = extract_ollama_action_json_with_repair(planner_text);
        if (root.contains("candidate_keys") && root["candidate_keys"].is_array()) {
            for (const auto& k : root["candidate_keys"]) {
                if (k.is_string())
                    keys.push_back(k.get<std::string>());
            }
        }
    } catch (...) {
    }
    return keys;
}

}} // namespace
