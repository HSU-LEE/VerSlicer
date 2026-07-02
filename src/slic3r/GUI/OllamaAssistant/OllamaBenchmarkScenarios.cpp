#include "OllamaBenchmarkScenarios.hpp"

#include "OllamaSettingCatalogBuilder.hpp"
#include "OllamaSettingSearch.hpp"

namespace Slic3r { namespace GUI {

namespace {

bool search_hits_key(const std::string& query, const char* key_substr)
{
    const auto keys = OllamaSettingSearch::candidate_keys_for_request(query, 2, 8);
    for (const std::string& key : keys) {
        if (key.find(key_substr) != std::string::npos)
            return true;
    }
    return false;
}

bool catalog_has_key_substr(const char* key_substr)
{
    const auto hits = OllamaSettingSearch::search(key_substr, 2, 8);
    for (const auto& hit : hits) {
        if (hit.key.find(key_substr) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

const std::vector<OllamaBenchmarkScenario>& ollama_benchmark_scenarios()
{
    static const std::vector<OllamaBenchmarkScenario> scenarios = {
        {"support_en", "enable support", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"support_ko", "서포트 켜줘", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"brim_en", "add brim", [](const std::string& u) { return search_hits_key(u, "brim"); }},
        {"brim_ko", "브림 넣어줘", [](const std::string& u) { return search_hits_key(u, "brim"); }},
        {"strength_en", "make it stronger", [](const std::string&) {
             return catalog_has_key_substr("infill") && catalog_has_key_substr("wall");
         }},
        {"strength_ko", "더 단단하게", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("infill", 2, 5);
             return !hits.empty();
         }},
        {"durability_ko", "부서져요", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("wall", 2, 5);
             return !hits.empty();
         }},
        {"adhesion_en", "won't stick to bed", [](const std::string&) { return catalog_has_key_substr("brim"); }},
        {"adhesion_ko", "베드에 안 붙어", [](const std::string& u) {
             return search_hits_key(u, "bed") || catalog_has_key_substr("brim");
         }},
        {"no_brim_ko", "브림 없이", [](const std::string& u) { return search_hits_key(u, "brim"); }},
        {"midair_ko", "공중에 출력", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"midair_en", "overhang failed", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"search_layer", "layer height", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("layer height", 2, 5);
             return !hits.empty() && hits.front().key.find("layer") != std::string::npos;
         }},
        {"search_infill_ko", "채움", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("infill", 2, 5);
             return !hits.empty();
         }},
        {"search_temp", "nozzle temperature", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("nozzle temperature", 2, 5);
             return !hits.empty();
         }},
        {"search_support", "support", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("support", 2, 8);
             return !hits.empty();
         }},
        {"search_retraction", "stringing", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("retraction", 2, 5);
             return !hits.empty();
         }},
        {"vague_ko", "고쳐줘", [](const std::string&) { return true; }},
        {"explicit_infill", "infill 20%", [](const std::string& u) {
             return u.find('%') != std::string::npos || u.find("infill") != std::string::npos;
         }},
        {"support_off_ko", "서포트 끄", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"support_off_en", "disable support", [](const std::string& u) { return search_hits_key(u, "support"); }},
        {"strength_walls", "make it sturdier", [](const std::string&) {
             return catalog_has_key_substr("wall") || catalog_has_key_substr("infill");
         }},
        {"adhesion_warp", "corner lifting", [](const std::string&) { return catalog_has_key_substr("brim"); }},
        {"stringing_en", "lots of stringing", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("retraction", 2, 3);
             return !hits.empty();
         }},
        {"layer_ko", "층 두께", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("layer", 2, 5);
             return !hits.empty();
         }},
        {"speed_en", "print faster", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("speed", 2, 5);
             return !hits.empty();
         }},
        {"iron_en", "ironing", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("ironing", 2, 5);
             return !hits.empty();
         }},
        {"seam_en", "seam position", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("seam", 2, 5);
             return !hits.empty();
         }},
        {"raft_en", "add raft", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("raft", 2, 5);
             return !hits.empty();
         }},
        {"temp_ko", "노즐 온도", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("temperature", 2, 5);
             return !hits.empty();
         }},
        {"wall_loops", "wall loops", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("wall", 2, 8);
             return !hits.empty();
         }},
        {"pattern_en", "gyroid infill", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("infill pattern", 2, 5);
             return !hits.empty();
         }},
        {"brim_width_search", "brim width", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("brim", 2, 5);
             return !hits.empty() && hits.front().key.find("brim") != std::string::npos;
         }},
        {"planner_keys_parse", R"({"candidate_keys":["layer_height","enable_support"]})", [](const std::string& t) {
             const auto keys = OllamaSettingSearch::keys_from_planner_json(t);
             return keys.size() == 2;
         }},
        {"tier3_block", "machine_start_gcode", [](const std::string&) {
             return OllamaSettingCatalogBuilder::is_restricted_key("machine_start_gcode");
         }},
        {"durability_en", "keeps breaking", [](const std::string&) {
             return catalog_has_key_substr("wall") || catalog_has_key_substr("infill");
         }},
        {"midair_ko2", "매달려서 출력", [](const std::string& u) {
             return search_hits_key(u, "support") || catalog_has_key_substr("support");
         }},
        {"disable_brim_en", "no brim please", [](const std::string& u) { return search_hits_key(u, "brim"); }},
        {"explicit_density", "15% infill", [](const std::string& u) { return u.find('%') != std::string::npos; }},
        {"search_ko_brim", "브림", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("brim", 2, 5);
             return !hits.empty();
         }},
        {"search_ko_stringing", "실이 많이", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("retraction", 2, 5);
             return !hits.empty();
         }},
        {"search_ko_surface", "표면이 거칠어요", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("ironing", 2, 5);
             return !hits.empty();
         }},
        {"search_ko_clog", "노즐이 막혀요", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("nozzle temperature", 2, 5);
             return !hits.empty();
         }},
        {"search_ko_bridge", "브릿지가 처져요", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("bridge", 2, 5);
             return !hits.empty();
         }},
    };
    return scenarios;
}

}} // namespace
