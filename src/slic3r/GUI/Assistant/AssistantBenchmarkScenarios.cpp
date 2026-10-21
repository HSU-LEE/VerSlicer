#include "AssistantBenchmarkScenarios.hpp"

#include "AssistantSettingCatalogBuilder.hpp"
#include "AssistantSettingSearch.hpp"

namespace Slic3r { namespace GUI {

namespace {

bool search_hits_key(const std::string& query, const char* key_substr)
{
    const auto keys = AssistantSettingSearch::candidate_keys_for_request(query, 2, 8);
    for (const std::string& key : keys) {
        if (key.find(key_substr) != std::string::npos)
            return true;
    }
    return false;
}

bool catalog_has_key_substr(const char* key_substr)
{
    const auto hits = AssistantSettingSearch::search(key_substr, 2, 8);
    for (const auto& hit : hits) {
        if (hit.key.find(key_substr) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

const std::vector<AssistantBenchmarkScenario>& assistant_benchmark_scenarios()
{
    static const std::vector<AssistantBenchmarkScenario> scenarios = {
        {"support_en", "enable support", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"brim_en", "add brim", [](const std::string& u) { return search_hits_key(u, "brim"); }},
        {"strength_en", "make it stronger", [](const std::string&) {
             return catalog_has_key_substr("infill") && catalog_has_key_substr("wall");
         }},
        {"adhesion_en", "won't stick to bed", [](const std::string&) { return catalog_has_key_substr("brim"); }},
        {"midair_en", "overhang failed", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"search_layer", "layer height", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("layer height", 2, 5);
             return !hits.empty() && hits.front().key.find("layer") != std::string::npos;
         }},
        {"search_temp", "nozzle temperature", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("nozzle temperature", 2, 5);
             return !hits.empty();
         }},
        {"search_support", "support", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("support", 2, 8);
             return !hits.empty();
         }},
        {"search_retraction", "stringing", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("retraction", 2, 5);
             return !hits.empty();
         }},
        {"explicit_infill", "infill 20%", [](const std::string& u) {
             return u.find('%') != std::string::npos || u.find("infill") != std::string::npos;
         }},
        {"support_off_en", "disable support", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"strength_walls", "make it sturdier", [](const std::string&) {
             return catalog_has_key_substr("wall") || catalog_has_key_substr("infill");
         }},
        {"adhesion_warp", "corner lifting", [](const std::string&) { return catalog_has_key_substr("brim"); }},
        {"stringing_en", "lots of stringing", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("retraction", 2, 3);
             return !hits.empty();
         }},
        {"speed_en", "print faster", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("speed", 2, 5);
             return !hits.empty();
         }},
        {"iron_en", "ironing", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("ironing", 2, 5);
             return !hits.empty();
         }},
        {"seam_en", "seam position", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("seam", 2, 5);
             return !hits.empty();
         }},
        {"raft_en", "add raft", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("raft", 2, 5);
             return !hits.empty();
         }},
        {"wall_loops", "wall loops", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("wall", 2, 8);
             return !hits.empty();
         }},
        {"pattern_en", "gyroid infill", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("infill pattern", 2, 5);
             return !hits.empty();
         }},
        {"brim_width_search", "brim width", [](const std::string&) {
             const auto hits = AssistantSettingSearch::search("brim", 2, 5);
             return !hits.empty() && hits.front().key.find("brim") != std::string::npos;
         }},
        {"planner_keys_parse", R"({"candidate_keys":["layer_height","enable_support"]})", [](const std::string& t) {
             const auto keys = AssistantSettingSearch::keys_from_planner_json(t);
             return keys.size() == 2;
         }},
        {"tier3_block", "machine_start_gcode", [](const std::string&) {
             return AssistantSettingCatalogBuilder::is_restricted_key("machine_start_gcode");
         }},
        {"durability_en", "keeps breaking", [](const std::string&) {
             return catalog_has_key_substr("wall") || catalog_has_key_substr("infill");
         }},
        {"disable_brim_en", "no brim please", [](const std::string& u) { return search_hits_key(u, "brim"); }},
        {"explicit_density", "15% infill", [](const std::string& u) { return u.find('%') != std::string::npos; }},
    };
    return scenarios;
}

}} // namespace
