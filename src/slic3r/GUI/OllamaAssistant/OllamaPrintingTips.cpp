#include "OllamaPrintingTips.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

namespace {

struct ProTip
{
    const char* id;
    const char* tags_en;
    const char* tags_ko;
    const char* tip_en;
    const char* tip_ko;
    const char* keys_csv;
};

static const ProTip kTips[] = {
    {"gyroid_strength", "strong strength durable infill", "튼튼 단단 강도 채움",
     "Gyroid or cubic infill gives better strength per gram than grid — try sparse_infill_pattern before maxing density.",
     "격자보다 gyroid·cubic 채움이 같은 무게로 더 단단합니다 — 밀도만 올리기 전에 sparse_infill_pattern을 바꿔 보세요.",
     "sparse_infill_pattern,sparse_infill_density"},
    {"ironing_top", "top surface rough shiny ironing", "윗면 표면 거칠 아이어링 윗층",
     "Ironing (ironing_type top) smooths the visible top without shrinking layer height — good when layer_height is already low.",
     "아이어링(ironing_type top)은 층 높이를 더 낮추지 않고 윗면을 매끈하게 합니다.",
     "ironing_type,top_shell_layers"},
    {"elephant_foot", "warp corner lift elephant foot", "들뜸 코너 warp 코너",
     "Corner lift often responds to elephant_foot_compensation or a wider brim before raising bed temp.",
     "모서리 들뜸은 베드 온도보다 elefant_foot_compensation이나 브림을 먼저 시도해 보세요.",
     "elefant_foot_compensation,brim_width"},
    {"min_layer_time", "small tiny layers melt droop", "작은 층 녹 droop 작은",
     "Tiny layers overheat — minimum_layer_time or slowing outer_wall_speed on small features prevents sag.",
     "작은 면적 층은 과열됩니다 — minimum_layer_time이나 outer_wall_speed를 낮추면 처짐이 줄어듭니다.",
     "minimum_layer_time,outer_wall_speed"},
    {"bridge_fan", "bridge bridging sag string", "브릿지 다리 sag",
     "Bridges need cooling — bridge_fan_speed and lower bridge_speed reduce sag without full supports.",
     "브릿지는 냉각이 중요합니다 — bridge_fan_speed·bridge_speed로 서포트 없이도 처짐을 줄일 수 있습니다.",
     "bridge_fan_speed,bridge_speed"},
    {"tree_support", "support hard remove tree", "서포트 제거 딱딱 tree",
     "Tree supports (support_type tree) ease removal on complex models vs normal supports.",
     "복잡한 모델은 tree 서포트(support_type)가 떼기 쉽습니다.",
     "support_type,enable_support"},
    {"retraction_travel", "stringing ooze travel", "실 stringing ooze 거미",
     "Stringing: raise retraction_length, enable retraction_when_crossing_perimeters, or lower nozzle temp slightly.",
     "실링: retraction_length, retraction_when_crossing_perimeters, 노즐 온도 소폭 하향을 조합해 보세요.",
     "retraction_length,retraction_when_crossing_perimeters,nozzle_temperature"},
    {"pressure_advance", "corner bulge blob sharp", "코너 bulge 뭉침 날카",
     "Corner bulging on fast prints — pressure_advance (filament) tunes flow at direction changes (advanced).",
     "빠른 출력 시 코너 뭉침은 pressure_advance(필라멘트)로 조절할 수 있습니다(고급).",
     "pressure_advance"},
    {"seam_position", "seam ugly line visible", "심 seam 선 보기",
     "Visible seam lines — try seam_position aligned/back or rotate model so seam hides on a corner.",
     "심(이음) 선이 보이면 seam_position을 바꾸거나 모델을 돌려 심을 모서리에 숨기세요.",
     "seam_position"},
    {"line_width_strength", "weak thin wall strength", "약함 벽 얇",
     "Wider line_width or +1 wall_loops often beats only raising infill for thin-walled parts.",
     "얇은 벽 파츠는 채움만 올리기보다 line_width나 wall_loops가 더 효과적일 때가 많습니다.",
     "line_width,wall_loops"},
    {"first_layer_squish", "first layer adhesion stick", "첫층 접착 안붙",
     "First-layer issues: initial_layer_print_height slightly thicker + slower initial_layer_speed often beats max brim.",
     "첫 층 문제는 브림만 키우기보다 initial_layer_print_height·initial_layer_speed 조합이 낫습니다.",
     "initial_layer_print_height,initial_layer_speed,brim_width"},
    {"overhang_speed", "overhang droop midair", "오버행 droop 매달",
     "Mild overhangs — lower overhang_speed or enable_support threshold before full tree supports.",
     "가벼운 오버행은 overhang_speed를 낮추거나 서포트 임계값을 조정해 보세요.",
     "overhang_speed,enable_support"},
    {"cooling_small", "pla curl fan", "pla curl 팬 냉각",
     "PLA curling on small parts — increase fan speed or minimum_layer_time so layers solidify before the next.",
     "작은 PLA 파츠 들뜸은 팬·minimum_layer_time으로 층이 굳을 시간을 주세요.",
     "fan_min_speed,minimum_layer_time"},
    {"orient_stress", "break snap orientation", "부러 파손 방향",
     "Parts snap along layer lines — rotate so stress goes across layers (lay flat) or add wall_loops on weak axis.",
     "층 사이로 부서지면 stress 방향으로 눕히거나(rotate) 약한 축에 wall_loops를 더하세요.",
     "rotate,wall_loops"},
    {"infill_combo", "strong fast balance", "튼튼 빠르 균형",
     "Strong + fast tradeoff: moderate gyroid infill + one extra wall beats 40% grid infill for many functional parts.",
     "튼튼+빠름 절충: gyroid 채움 중간 + 벽 1겹 추가가 40% 격자만 올리는 것보다 나을 때가 많습니다.",
     "sparse_infill_pattern,sparse_infill_density,wall_loops"},
};

std::string lower_ascii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int score_tip(const ProTip& tip, const std::string& query_lower)
{
    const std::string tags = lower_ascii(std::string(tip.tags_en) + " " + tip.tags_ko);
    int                 score = 0;
    for (size_t i = 0; i < query_lower.size();) {
        while (i < query_lower.size() && !std::isalnum(static_cast<unsigned char>(query_lower[i]))
               && static_cast<unsigned char>(query_lower[i]) < 128)
            ++i;
        if (i >= query_lower.size())
            break;
        size_t j = i;
        while (j < query_lower.size() && (std::isalnum(static_cast<unsigned char>(query_lower[j]))
                                          || static_cast<unsigned char>(query_lower[j]) >= 128))
            ++j;
        const std::string token = query_lower.substr(i, j - i);
        i                       = j;
        if (token.size() < 2)
            continue;
        if (tags.find(token) != std::string::npos)
            score += token.size() >= 4 ? 30 : 15;
    }
    return score;
}

} // namespace

nlohmann::json OllamaPrintingTips::tips_for_request(const std::string& user_request, bool korean, size_t limit)
{
    const std::string q = lower_ascii(user_request);
    std::vector<std::pair<int, const ProTip*>> ranked;
    ranked.reserve(sizeof(kTips) / sizeof(kTips[0]));
    for (const ProTip& tip : kTips) {
        const int s = score_tip(tip, q);
        if (s > 0)
            ranked.push_back({s, &tip});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second->id < b.second->id;
    });

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : ranked) {
        if (arr.size() >= limit)
            break;
        const ProTip& tip = *p.second;
        nlohmann::json e;
        e["id"]   = tip.id;
        e["tip"]  = korean ? tip.tip_ko : tip.tip_en;
        e["keys"] = tip.keys_csv;
        arr.push_back(std::move(e));
    }

    if (arr.empty()) {
        const size_t n = std::min(limit, sizeof(kTips) / sizeof(kTips[0]));
        for (size_t i = 0; i < n; ++i) {
            const ProTip& tip = kTips[i];
            nlohmann::json e;
            e["id"]   = tip.id;
            e["tip"]  = korean ? tip.tip_ko : tip.tip_en;
            e["keys"] = tip.keys_csv;
            arr.push_back(std::move(e));
        }
    }
    return arr;
}

}} // namespace
