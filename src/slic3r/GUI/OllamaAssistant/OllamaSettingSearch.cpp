#include "OllamaSettingSearch.hpp"

#include "OllamaActionJsonExtract.hpp"
#include "OllamaSettingAliases.hpp"
#include "OllamaSettingCatalogBuilder.hpp"

#include "libslic3r/PrintConfig.hpp"

#include <boost/algorithm/string.hpp>

#include <nlohmann/json.hpp>

#include <wx/intl.h>
#include <wx/string.h>

#include <algorithm>
#include <optional>
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

int score_phrase(const std::string& haystack, const std::string& query)
{
    int score = score_token(haystack, query);
    if (haystack.size() >= 2 && boost::icontains(query, haystack))
        score = std::max(score, 75);
    return score;
}

void consider_phrase(int& score, const std::string& haystack, const std::string& query)
{
    if (!haystack.empty())
        score = std::max(score, score_phrase(haystack, query));
}

void consider_translated(int& score, const std::string& source, const std::string& query)
{
    if (source.empty())
        return;
    consider_phrase(score, source, query);
    const wxString translated = wxGetTranslation(wxString::FromUTF8(source));
    if (!translated.empty())
        consider_phrase(score, translated.utf8_string(), query);
}

bool is_off_enum_value(const std::string& value)
{
    const std::string lower = boost::algorithm::to_lower_copy(value);
    return lower == "none" || lower == "off" || lower == "disabled" || lower == "false"
        || boost::algorithm::starts_with(lower, "no ");
}

bool goal_wants_on(const std::string& goal)
{
    return boost::contains(goal, "켜") || boost::icontains(goal, "enable") || boost::icontains(goal, "turn on");
}

bool goal_wants_off(const std::string& goal)
{
    return boost::contains(goal, "끄") || boost::contains(goal, "꺼") || boost::icontains(goal, "disable")
        || boost::icontains(goal, "turn off");
}

int score_spec(const OllamaAutoSettingSpec& sp, const std::string& query)
{
    int score = 0;
    consider_phrase(score, sp.key, query);
    consider_translated(score, sp.label, query);
    consider_translated(score, sp.category, query);
    consider_translated(score, sp.tooltip, query);
    if (sp.key.size() >= 4 && boost::icontains(query, sp.key))
        score = std::max(score, 65);
    for (const std::string& ko : OllamaSettingAliases::ko_terms_for_key(sp.key))
        consider_phrase(score, ko, query);
    if (const ConfigOptionDef* def = print_config_def.get(sp.key)) {
        for (const std::string& label : def->enum_labels)
            consider_translated(score, label, query);
        for (const std::string& value : def->enum_values)
            consider_phrase(score, value, query);
    }
    for (size_t pos = 0; pos < sp.key.size(); ++pos) {
        if (sp.key[pos] != '_')
            continue;
        const std::string part = sp.key.substr(pos + 1);
        if (part.size() >= 4 && boost::icontains(query, part))
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
        if (const ConfigOptionDef* def = print_config_def.get(sp->key)) {
            if (!def->enum_values.empty())
                e["enum_values"] = def->enum_values;
            if (!def->enum_labels.empty()) {
                nlohmann::json labels = nlohmann::json::array();
                for (const std::string& label : def->enum_labels)
                    labels.push_back(wxGetTranslation(wxString::FromUTF8(label)).utf8_string());
                e["enum_labels"] = std::move(labels);
            }
        }
        if (cfg && cfg->has(sp->key))
            e["current"] = cfg->opt_serialize(sp->key);
        else
            e["current"] = nullptr;
        (void) ko_ui;
        arr.push_back(std::move(e));
    }
    return arr;
}

std::optional<OllamaObviousSettingChange> OllamaSettingSearch::obvious_toggle(const std::string& user_goal)
{
    const bool on  = goal_wants_on(user_goal);
    const bool off = goal_wants_off(user_goal);
    if (on == off)
        return std::nullopt;

    const std::vector<OllamaSettingSearchHit> hits = search(user_goal, 2, 8);
    std::vector<OllamaSettingSearchHit> strong;
    for (const OllamaSettingSearchHit& hit : hits) {
        if (hit.score >= 75)
            strong.push_back(hit);
    }
    if (strong.empty())
        return std::nullopt;
    const int top = strong.front().score;
    int       close = 0;
    for (const OllamaSettingSearchHit& hit : strong) {
        if (top - hit.score <= 10)
            ++close;
    }
    if (close != 1)
        return std::nullopt;

    const OllamaAutoSettingSpec* sp = OllamaSettingCatalogBuilder::find(strong.front().key);
    if (!sp || sp->virtual_key || sp->tier == OllamaSettingTier::Restricted)
        return std::nullopt;

    OllamaObviousSettingChange change;
    change.preset = sp->preset_scope.empty() ? "print" : sp->preset_scope;
    change.key    = sp->key;
    change.label  = wxGetTranslation(wxString::FromUTF8(sp->label.empty() ? sp->key : sp->label)).utf8_string();

    if (sp->value_type == "bool") {
        change.value = on;
        return change;
    }

    const ConfigOptionDef* def = print_config_def.get(sp->key);
    if (!def || def->enum_values.empty())
        return std::nullopt;

    if (off) {
        for (const std::string& value : def->enum_values) {
            if (is_off_enum_value(value)) {
                change.value = value;
                return change;
            }
        }
        return std::nullopt;
    }

    for (const std::string& value : def->enum_values) {
        if (!is_off_enum_value(value)) {
            change.value = value;
            return change;
        }
    }
    return std::nullopt;
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
