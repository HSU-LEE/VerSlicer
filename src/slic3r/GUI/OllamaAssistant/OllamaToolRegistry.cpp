#include "OllamaToolRegistry.hpp"

#include "OllamaActionRegistry.hpp"

namespace Slic3r { namespace GUI {

namespace {

const OllamaToolSpec kTools[] = {
    {"get_state", OllamaToolCategory::Readonly, "Read current slicer state (selection, plates, slice)", "현재 슬라이서 상태 읽기"},
    {"list_objects", OllamaToolCategory::Readonly, "List models on the plate", "플레이트 모델 목록"},
    {"select_object", OllamaToolCategory::Mutating, "Select object by id or name", "객체 선택"},
    {"set_config", OllamaToolCategory::Dangerous, "Change print settings", "설정 변경"},
    {"arrange", OllamaToolCategory::Mutating, "Auto-arrange on plate", "자동 배치"},
    {"rotate", OllamaToolCategory::Mutating, "Rotate selection", "회전"},
    {"translate", OllamaToolCategory::Mutating, "Move selection", "이동"},
    {"slice", OllamaToolCategory::Mutating, "Slice current plate", "슬라이스"},
    {"ui_select_tab", OllamaToolCategory::Mutating, "Switch main tab", "탭 전환"},
    {"open_calibration", OllamaToolCategory::Mutating, "Open calibration tab", "캘리브레이션 탭"},
    {"export_gcode", OllamaToolCategory::Dangerous, "Export G-code", "G-code보내기"},
    {"send_print", OllamaToolCategory::Dangerous, "Send to printer", "프린터 전송"},
    {"add_plate", OllamaToolCategory::Mutating, "Add build plate", "플레이트 추가"},
    {"delete_plate", OllamaToolCategory::Mutating, "Delete current plate", "플레이트 삭제"},
    {"select_plate", OllamaToolCategory::Mutating, "Select plate by index", "플레이트 선택"},
    {"save_project", OllamaToolCategory::Dangerous, "Save project", "프로젝트 저장"},
    {"select_preset", OllamaToolCategory::Dangerous, "Select print/filament/printer preset", "프리셋 선택"},
    {"run_smart_print", OllamaToolCategory::Mutating, "Open Smart Print workflow", "스마트 프린트"},
    {"makerworld_search", OllamaToolCategory::Mutating, "Search MakerWorld", "MakerWorld 검색"},
    {"rollback_apply", OllamaToolCategory::Mutating, "Undo last AI apply", "AI 되돌리기"},
};

} // namespace

const std::vector<OllamaToolSpec>& OllamaToolRegistry::all()
{
    static const std::vector<OllamaToolSpec> specs(std::begin(kTools), std::end(kTools));
    return specs;
}

OllamaToolCategory OllamaToolRegistry::category_for(const std::string& tool_id)
{
    for (const auto& t : all()) {
        if (tool_id == t.id)
            return t.category;
    }
    if (OllamaActionRegistry::is_allowed_type(tool_id))
        return OllamaToolCategory::Mutating;
    return OllamaToolCategory::Dangerous;
}

bool OllamaToolRegistry::is_agent_tool(const std::string& tool_id)
{
    if (tool_id == "get_state" || tool_id == "list_objects")
        return true;
    return OllamaActionRegistry::is_allowed_type(tool_id);
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
          "목표 달성 시 done:true 와 요약 message.\n"
        : "\nAgent reply JSON: {\"message\":\"...\", \"done\":false, \"actions\":[{\"type\":\"get_state\"}, ...]}\n"
          "Set done:true with summary when the goal is complete.\n";
    return out;
}

}} // namespace
