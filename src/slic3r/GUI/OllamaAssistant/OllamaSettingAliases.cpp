#include "OllamaSettingAliases.hpp"

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

bool query_contains(const std::string& query_lower, const char* needle)
{
    return !needle || !*needle || query_lower.find(needle) != std::string::npos;
}

const std::unordered_map<std::string, std::vector<const char*>>& ko_alias_table()
{
    static const std::unordered_map<std::string, std::vector<const char*>> table = {
        {"layer_height", {"층", "층 높이", "레이어", "두께"}},
        {"initial_layer_print_height", {"첫 층", "첫층", "first layer"}},
        {"sparse_infill_density", {"채움", "채움률", "인필", "infill", "속 채움"}},
        {"sparse_infill_pattern", {"채움 패턴", "infill pattern", "gyroid", "grid"}},
        {"wall_loops", {"벽", "외벽", "wall", "perimeter", "껍질"}},
        {"enable_support", {"서포트", "받침", "support", "overhang", "공중"}},
        {"brim_width", {"브림", "brim", "가장자리", "접착", "edge"}},
        {"brim_type", {"브림 종류", "brim type"}},
        {"raft_layers", {"뗏목", "raft"}},
        {"retraction_length", {"리트랙션", "retraction", "실", "stringing", "끈"}},
        {"retraction_speed", {"리트랙션 속도", "retraction speed"}},
        {"nozzle_temperature", {"노즐", "온도", "temperature", "temp"}},
        {"bed_temperature", {"베드", "히트베드", "bed temp", "bed temperature"}},
        {"outer_wall_speed", {"외벽 속도", "outer wall", "wall speed"}},
        {"sparse_infill_speed", {"채움 속도", "infill speed"}},
        {"seam_position", {"솔기", "seam"}},
        {"ironing_type", {"아이어링", "ironing"}},
        {"elefant_foot_compensation", {"코끼리발", "elephant foot"}},
        {"support_top_z_distance", {"서포트 간격", "support gap"}},
        {"support_type", {"서포트 종류", "support type", "tree support"}},
    };
    return table;
}

struct SymptomRule
{
    const char* phrases[4];
    const char* keys[6];
    int         boost;
};

const std::vector<SymptomRule>& symptom_rules()
{
    static const std::vector<SymptomRule> rules = {
        {{"베드", "안 붙", "stick", "adhesion"}, {"brim_width", "brim_type", "bed_temperature", nullptr}, 85},
        {{"들뜸", "warp", "curl", "lift"}, {"brim_width", "bed_temperature", nullptr}, 80},
        {{"공중", "overhang", "floating", "매달"}, {"enable_support", "support_type", nullptr}, 85},
        {{"실", "stringing", "끈", "ooze"}, {"retraction_length", "nozzle_temperature", nullptr}, 82},
        {{"부서", "brittle", "break", "약"}, {"sparse_infill_density", "wall_loops", nullptr}, 78},
        {{"단단", "strong", "sturdy", "튼튼"}, {"sparse_infill_density", "wall_loops", nullptr}, 75},
        {{"올려", "올리", "increase infill", "more infill"}, {"sparse_infill_density", nullptr}, 82},
        {{"느리", "slow", "speed up", "빠르"}, {"outer_wall_speed", "sparse_infill_speed", nullptr}, 70},
        {{"첫층", "first layer", "initial layer"}, {"initial_layer_print_height", "layer_height", nullptr}, 80},
        {{"솔기", "seam", "visible seam"}, {"seam_position", nullptr}, 75},
        {{"아이어링", "ironing", "top surface"}, {"ironing_type", "ironing_flow", nullptr}, 75},
        {{"코끼리", "elephant foot"}, {"elefant_foot_compensation", nullptr}, 80},
    };
    return rules;
}

} // namespace

std::vector<std::string> OllamaSettingAliases::ko_terms_for_key(const std::string& key)
{
    std::vector<std::string> out;
    const auto               it = ko_alias_table().find(key);
    if (it == ko_alias_table().end())
        return out;
    for (const char* term : it->second) {
        if (term && *term)
            out.emplace_back(term);
    }
    return out;
}

int OllamaSettingAliases::symptom_boost(const std::string& query_lower, const std::string& key)
{
    int best = 0;
    for (const SymptomRule& rule : symptom_rules()) {
        bool matched = false;
        for (const char* phrase : rule.phrases) {
            if (!phrase)
                break;
            if (query_contains(query_lower, phrase)) {
                matched = true;
                break;
            }
        }
        if (!matched)
            continue;
        for (const char* rk : rule.keys) {
            if (!rk)
                break;
            if (key == rk)
                best = std::max(best, rule.boost);
        }
    }
    return best;
}

std::vector<std::string> OllamaSettingAliases::keys_from_symptoms(const std::string& query)
{
    std::vector<std::string>             keys;
    std::unordered_map<std::string, int> scores;
    for (const SymptomRule& rule : symptom_rules()) {
        bool matched = false;
        for (const char* phrase : rule.phrases) {
            if (!phrase)
                break;
            if (query_contains(query, phrase)) {
                matched = true;
                break;
            }
        }
        if (!matched)
            continue;
        for (const char* rk : rule.keys) {
            if (!rk)
                break;
            scores[rk] = std::max(scores[rk], rule.boost);
        }
    }
    keys.reserve(scores.size());
    for (const auto& kv : scores)
        keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end(), [&](const std::string& a, const std::string& b) {
        if (scores[a] != scores[b])
            return scores[a] > scores[b];
        return a < b;
    });
    return keys;
}

}} // namespace
