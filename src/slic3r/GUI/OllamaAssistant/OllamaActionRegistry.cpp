#include "OllamaActionRegistry.hpp"

#include <boost/algorithm/string.hpp>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

const OllamaActionTypeSpec kActionTypes[] = {
    {"get_state", true, true, true, "Read current slicer state", "현재 슬라이서 상태 읽기"},
    {"list_objects", true, true, true, "List models on the plate", "플레이트 모델 목록"},
    {"select_object", true, true, true, "Select object by id or name", "객체 선택"},
    {"set_config", true, true, true, "Change print/filament/printer preset options", "프리셋 설정 변경"},
    {"translate", true, true, true, "Move selection on the bed", "선택 모델 이동"},
    {"rotate", true, true, true, "Rotate selection", "선택 모델 회전"},
    {"scale", true, false, false, "Scale selection", "선택 모델 크기 조절"},
    {"clone_selection", true, true, true, "Duplicate selection", "선택 복제"},
    {"arrange", true, true, true, "Auto-arrange on build plate", "플레이트 자동 배치"},
    {"delete_selection", true, false, false, "Delete selected models", "선택 삭제"},
    {"ui_select_tab", true, false, false, "Switch main tab", "탭 전환"},
    {"open_calibration", true, false, false, "Open calibration tab", "캘리브레이션 탭"},
    {"slice", true, false, false, "Slice current plate", "슬라이스"},
    {"add_model", true, false, false, "Import local model file", "로컬 모델 추가"},
    {"makerworld_search", true, false, false, "Search MakerWorld catalog", "MakerWorld 검색"},
    {"import_makerworld", true, false, false, "Import from MakerWorld", "MakerWorld 가져오기"},
    {"open_smart_print", true, false, true, "Open Smart Print tab/panel", "스마트 프린트 열기"},
    {"open_setup", true, false, true, "Open Smart Print setup wizard", "프린터 설정/연결"},
    {"send_print", true, false, true, "Send sliced job to printer", "프린터로 출력"},
    {"export_gcode", true, false, true, "Export G-code file", "G-code 내보내기"},
    {"add_plate", true, false, false, "Add build plate", "플레이트 추가"},
    {"delete_plate", true, false, false, "Delete current plate", "플레이트 삭제"},
    {"select_plate", true, false, false, "Select plate by index", "플레이트 선택"},
    {"save_project", true, false, false, "Save project", "프로젝트 저장"},
    {"select_preset", true, false, false, "Select print/filament/printer preset", "프리셋 선택"},
    {"run_smart_print", true, false, true, "Run Smart Print workflow", "스마트 프린트 실행"},
    {"rollback_apply", true, false, true, "Undo last AI settings apply", "AI 설정 되돌리기"},
};

const std::unordered_set<std::string> kBlockedTypes = {
    "quit",
    "exit",
    "menu_item",
};

} // namespace

const std::vector<OllamaActionTypeSpec>& OllamaActionRegistry::all()
{
    static const std::vector<OllamaActionTypeSpec> specs(std::begin(kActionTypes), std::end(kActionTypes));
    return specs;
}

bool OllamaActionRegistry::is_blocked_type(const std::string& type)
{
    return kBlockedTypes.find(type) != kBlockedTypes.end();
}

bool OllamaActionRegistry::is_allowed_type(const std::string& type)
{
    if (is_blocked_type(type))
        return false;
    for (const auto& spec : all()) {
        if (type == spec.type)
            return true;
    }
    return false;
}

bool OllamaActionRegistry::is_allowed_in_advisor(const std::string& type)
{
    if (is_blocked_type(type))
        return false;
    for (const auto& spec : all()) {
        if (type == spec.type)
            return spec.allowed_in_advisor;
    }
    return false;
}

std::string OllamaActionRegistry::action_types_prompt_block(bool ko_ui, bool apply_mode)
{
    std::string out;
    for (const auto& spec : all()) {
        if (!apply_mode && spec.type != std::string("set_config"))
            continue;
        if (apply_mode && !spec.allowed_in_apply)
            continue;
        out += "- ";
        out += spec.type;
        out += ": ";
        out += ko_ui ? spec.desc_ko : spec.desc_en;
        out += "\n";
    }
    return out;
}

}} // namespace
