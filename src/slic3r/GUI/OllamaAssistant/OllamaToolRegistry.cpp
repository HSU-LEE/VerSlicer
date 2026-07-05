#include "OllamaToolRegistry.hpp"

#include "OllamaActionRegistry.hpp"
#include "OllamaActionValidator.hpp"

namespace Slic3r { namespace GUI {

namespace {

const OllamaToolSpec kTools[] = {
    {"get_state", OllamaToolCategory::Readonly, "Read current slicer state (selection, plates, slice)", "현재 슬라이서 상태 읽기"},
    {"list_objects", OllamaToolCategory::Readonly, "List models on the plate", "플레이트 모델 목록"},
    {"select_object", OllamaToolCategory::Mutating, "Select object by id or name", "객체 선택"},
    {"set_config", OllamaToolCategory::Dangerous, "Change print settings", "설정 변경"},
    {"arrange", OllamaToolCategory::Mutating, "Auto-arrange on plate (by layer)", "플레이트 자동 배치 (층별)"},
    {"arrange_objects", OllamaToolCategory::Mutating, "Arrange by object / print-by-object layout", "객체 단위 재배치 (물체 객체로)"},
    {"split_mesh", OllamaToolCategory::Mutating, "Split model into separate objects", "모델을 개별 객체로 분할"},
    {"split_object", OllamaToolCategory::Mutating, "Split model into separate objects (alias)", "모델 분할 (별칭)"},
    {"rotate", OllamaToolCategory::Mutating, "Rotate selection", "회전"},
    {"translate", OllamaToolCategory::Mutating, "Move selection", "이동"},
    {"slice", OllamaToolCategory::Mutating, "Slice current plate", "슬라이스"},
    {"ui_select_tab", OllamaToolCategory::Mutating, "Switch main tab", "탭 전환"},
    {"open_calibration", OllamaToolCategory::Mutating, "Open calibration tab", "캘리브레이션 탭"},
    {"export_gcode", OllamaToolCategory::Dangerous, "Export G-code", "G-code보내기"},
    {"send_print", OllamaToolCategory::Dangerous, "Send to printer", "프린터 전송"},
    {"add_plate", OllamaToolCategory::Mutating, "Add a new build plate (palette)", "빌드 플레이트(팔레트) 추가"},
    {"delete_plate", OllamaToolCategory::Mutating, "Delete current plate", "플레이트 삭제"},
    {"select_plate", OllamaToolCategory::Mutating, "Select plate by index", "플레이트 선택"},
    {"save_project", OllamaToolCategory::Dangerous, "Save project", "프로젝트 저장"},
    {"select_preset", OllamaToolCategory::Dangerous, "Select print/filament/printer preset", "프리셋 선택"},
    {"run_smart_print", OllamaToolCategory::Mutating, "Open Smart Print workflow", "스마트 프린트"},
    {"makerworld_search", OllamaToolCategory::Mutating, "Search MakerWorld", "MakerWorld 검색"},
    {"makerworld_find_and_print", OllamaToolCategory::Mutating,
     "Search MakerWorld (top 3), offer print with countdown, import+slice+send",
     "MakerWorld 상위 3개 검색 후 출력 제안(카운트다운) 및 불러오기·슬라이스·출력"},
    {"rollback_apply", OllamaToolCategory::Mutating, "Undo last AI apply", "AI 되돌리기"},
    {"scale", OllamaToolCategory::Mutating, "Resize model. 50% smaller → factor 0.5; 200% → 2.0. NOT set_config.", "모델 크기 조절. 50% 축소→factor 0.5, 200%→2.0. set_config 금지"},
    {"clone_selection", OllamaToolCategory::Mutating, "Duplicate selection", "복제"},
    {"delete_selection", OllamaToolCategory::Dangerous, "Delete selection (only if user asked)", "삭제"},
    {"repair_mesh", OllamaToolCategory::Mutating, "Repair mesh defects (check mesh_health first)", "메쉬 수리"},
    {"mirror_mesh", OllamaToolCategory::Mutating, "Mirror mesh", "메쉬 대칭"},
    {"mesh_boolean", OllamaToolCategory::Mutating, "Drill hole (subtract_cylinder) or add handle/rib. NOT set_config or slice.", "구멍 뚫기(subtract_cylinder)·손잡이. set_config/slice 금지"},
};

} // namespace

const std::vector<OllamaToolSpec>& OllamaToolRegistry::all()
{
    static const std::vector<OllamaToolSpec> specs(std::begin(kTools), std::end(kTools));
    return specs;
}

OllamaToolCategory OllamaToolRegistry::category_for(const std::string& tool_id)
{
    const std::string canon = OllamaActionValidator::canonical_action_type(tool_id);
    for (const auto& t : all()) {
        if (tool_id == t.id || canon == t.id)
            return t.category;
    }
    if (OllamaActionRegistry::is_allowed_type(canon))
        return OllamaToolCategory::Mutating;
    return OllamaToolCategory::Dangerous;
}

bool OllamaToolRegistry::is_agent_tool(const std::string& tool_id)
{
    if (tool_id == "get_state" || tool_id == "list_objects")
        return true;
    const std::string canon = OllamaActionValidator::canonical_action_type(tool_id);
    return OllamaActionRegistry::is_allowed_type(canon);
}

std::string OllamaToolRegistry::agent_tools_schema_block(bool korean)
{
    std::string out = korean ? "사용 가능한 도구 (actions[].type):\n" : "Available tools (actions[].type):\n";
    for (const auto& t : all()) {
        out += "- ";
        out += t.id;
        out += ": ";
        out += korean ? t.desc_ko : t.desc_en;
        out += "\n";
    }
    out += korean
        ? "\n에이전트 응답 JSON: {\"message\":\"...\", \"done\":false, \"actions\":[{\"type\":\"get_state\"}, ...]}\n"
          "형상 요청(크기·회전·구멍)은 scale/rotate/mesh_boolean 등만 사용 — set_config·slice 금지(슬라이스 요청 시 제외).\n"
          "목표 달성 시 done:true 와 요약 message.\n"
        : "\nAgent reply JSON: {\"message\":\"...\", \"done\":false, \"actions\":[{\"type\":\"get_state\"}, ...]}\n"
          "Model-shape requests (resize, rotate, hole) use scale/rotate/mesh_boolean only — no set_config or slice unless user asked to slice.\n"
          "Set done:true with summary when the goal is complete.\n";
    return out;
}

}} // namespace
