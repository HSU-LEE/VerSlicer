#include "OllamaSettingSearch.hpp"

#include "OllamaActionJsonExtract.hpp"
#include "OllamaSettingAliases.hpp"
#include "OllamaSettingCatalogBuilder.hpp"

#include "libslic3r/PrintConfig.hpp"

#include <boost/algorithm/string.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_map>

namespace Slic3r { namespace GUI {

namespace {

int score_token(const std::string& haystack, const std::string& token)
{
    if (token.empty())
        return 0;
    if (boost::iequals(haystack, token))
        return 100;
    if (boost::icontains(haystack, token))
        return 60;
    return 0;
}

int score_spec(const OllamaAutoSettingSpec& sp, const std::string& query)
{
    int score = score_token(sp.key, query);
    score     = std::max(score, score_token(sp.label, query));
    score     = std::max(score, score_token(sp.category, query));
    score     = std::max(score, score_token(sp.tooltip, query));
    if (boost::icontains(query, sp.key))
        score = std::max(score, 65);
    for (const std::string& ko : OllamaSettingAliases::ko_terms_for_key(sp.key)) {
        score = std::max(score, score_token(ko, query));
        if (boost::icontains(query, ko))
            score = std::max(score, 75);
    }
    for (size_t pos = 0; pos < sp.key.size(); ++pos) {
        if (sp.key[pos] != '_')
            continue;
        const std::string part = sp.key.substr(pos + 1);
        if (part.size() >= 3 && boost::icontains(query, part))
            score = std::max(score, 60);
    }
    return score;
}

} // namespace

std::vector<OllamaSettingSearchHit> OllamaSettingSearch::search(const std::string& query, int max_tier,
                                                                 size_t limit)
{
    if (query.empty())
        return {};

    std::vector<OllamaSettingSearchHit> hits;
    for (const OllamaAutoSettingSpec& sp : OllamaSettingCatalogBuilder::all()) {
        if (static_cast<int>(sp.tier) > max_tier || sp.virtual_key)
            continue;
        const int score = score_spec(sp, query);
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
    std::vector<std::string> keys;
    for (const OllamaSettingSearchHit& hit : search(query, max_tier, limit)) {
        if (keys.size() >= limit)
            break;
        keys.push_back(hit.key);
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
