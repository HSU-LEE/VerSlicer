#include "OllamaSettingAliases.hpp"

#include <unordered_map>

namespace Slic3r { namespace GUI {

namespace {

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
        {"retraction_length", {"리트랙션", "retraction"}},
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

}} // namespace
