#include "OllamaBenchmarkScenarios.hpp"

#include "OllamaIntentRules.hpp"
#include "OllamaSettingCatalogBuilder.hpp"
#include "OllamaRequestRouter.hpp"
#include "OllamaSettingSearch.hpp"

namespace Slic3r { namespace GUI {

namespace {

using namespace OllamaIntentRules;

} // namespace

const std::vector<OllamaBenchmarkScenario>& ollama_benchmark_scenarios()
{
    static const std::vector<OllamaBenchmarkScenario> scenarios = {
        {"support_en", "enable support", [](const std::string&) { return contains_support_intent("enable support"); }},
        {"support_ko", "서포트 켜줘", [](const std::string&) { return contains_support_intent("서포트 켜줘"); }},
        {"brim_en", "add brim", [](const std::string&) { return contains_brim_intent("add brim"); }},
        {"brim_ko", "브림 넣어줘", [](const std::string&) { return contains_brim_intent("브림"); }},
        {"strength_en", "make it stronger", [](const std::string&) { return contains_strength_intent("make it stronger"); }},
        {"strength_ko", "더 단단하게", [](const std::string&) { return contains_strength_intent("더 단단하게"); }},
        {"durability_ko", "부서져요", [](const std::string&) { return contains_durability_intent("부서져요"); }},
        {"adhesion_en", "won't stick to bed", [](const std::string&) { return contains_adhesion_intent("won't stick"); }},
        {"adhesion_ko", "베드에 안 붙어", [](const std::string&) { return contains_adhesion_intent("베드에 안 붙"); }},
        {"rotate_90_ko", "90도 돌려", [](const std::string&) {
             const auto z = parse_z_rotation_degrees("90도 돌려");
             return z.has_value() && *z == 90.0;
         }},
        {"rotate_left_ko", "왼쪽으로 45도", [](const std::string&) {
             const auto z = parse_z_rotation_degrees("왼쪽으로 45도");
             return z.has_value() && *z == -45.0;
         }},
        {"flip_en", "flip it", [](const std::string&) { return contains_flip_intent("flip it"); }},
        {"place_en", "arrange the plate", [](const std::string&) { return contains_placement_intent("arrange"); }},
        {"place_ko", "정렬해줘", [](const std::string&) { return contains_placement_intent("정렬"); }},
        {"no_brim_ko", "브림 없이", [](const std::string&) { return contains_disable_brim_intent("브림 없이"); }},
        {"midair_ko", "공중에 출력", [](const std::string&) { return contains_midair_or_failure_intent("공중"); }},
        {"midair_en", "overhang failed", [](const std::string&) { return contains_midair_or_failure_intent("overhang"); }},
        {"quality_not_place", "print failed arrange", [](const std::string&) {
             return describes_print_quality_symptom("print failed") && contains_placement_intent("arrange");
         }},
        {"delete_en", "delete selection", [](const std::string&) { return user_wants_delete("delete"); }},
        {"delete_ko", "삭제해", [](const std::string&) { return user_wants_delete("삭제"); }},
        {"search_layer", "layer height", [](const std::string& hint) {
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
        {"explicit_infill", "infill 20%", [](const std::string&) { return contains_explicit_infill_intent("infill 20%"); }},
        {"rotate_en", "rotate 90 degrees", [](const std::string&) { return contains_rotate_intent("rotate 90"); }},
        {"rotate_ko2", "45도 회전", [](const std::string&) { return contains_rotate_intent("45도"); }},
        {"support_off_ko", "서포트 끄", [](const std::string&) { return contains_support_intent("서포트"); }},
        {"support_off_en", "disable support", [](const std::string&) { return contains_support_intent("support"); }},
        {"strength_walls", "make it sturdier", [](const std::string&) { return contains_strength_intent("sturdy"); }},
        {"adhesion_warp", "corner lifting", [](const std::string&) { return contains_adhesion_intent("lifting"); }},
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
        {"flip_ko", "뒤집어줘", [](const std::string&) { return contains_flip_intent("뒤집"); }},
        {"durability_en", "keeps breaking", [](const std::string&) { return contains_durability_intent("breaking"); }},
        {"midair_ko2", "매달려서 출력", [](const std::string&) { return contains_midair_or_failure_intent("매달"); }},
        {"quality_only", "parts keep breaking", [](const std::string&) { return contains_durability_intent("breaking"); }},
        {"arrange_not_quality", "arrange models", [](const std::string&) {
             return contains_placement_intent("arrange") && !describes_print_quality_symptom("arrange models");
         }},
        {"disable_brim_en", "no brim please", [](const std::string&) { return contains_disable_brim_intent("no brim"); }},
        {"explicit_density", "15% infill", [](const std::string&) { return contains_explicit_infill_intent("15%"); }},
        {"search_ko_brim", "브림", [](const std::string&) {
             const auto hits = OllamaSettingSearch::search("brim", 2, 5);
             return !hits.empty();
         }},
        {"search_ko_stringing", "실이 많이", [](const std::string&) {
             const auto keys = OllamaSettingSearch::candidate_keys_for_request("실이 많이", 2, 5);
             return !keys.empty();
         }},
        {"route_support", "enable support", [](const std::string&) {
             return OllamaRequestRouter::classify("enable support") == OllamaRequestRoute::Standard;
         }},
        {"route_vague", "고쳐줘", [](const std::string&) {
             return OllamaRequestRouter::classify("고쳐줘") == OllamaRequestRoute::Deep;
         }},
    };
    return scenarios;
}

}} // namespace
