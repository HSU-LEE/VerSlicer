#include "BambuErrorCatalog.hpp"
#include "BambuSmartPrintPaths.hpp"

#include "libslic3r/Utils.hpp"
#include "nlohmann/json.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <unordered_map>
#include <mutex>
#include <tuple>

namespace Slic3r {
namespace BambuSmartPrint {

using json = nlohmann::json;
namespace fs = boost::filesystem;

static FailureCategory category_from_string(const std::string& s)
{
    if (s == "adhesion") return FailureCategory::Adhesion;
    if (s == "filament") return FailureCategory::Filament;
    if (s == "temperature") return FailureCategory::Temperature;
    if (s == "mechanical") return FailureCategory::Mechanical;
    if (s == "gcode") return FailureCategory::Gcode;
    if (s == "network") return FailureCategory::Network;
    if (s == "user_cancelled") return FailureCategory::UserCancelled;
    return FailureCategory::Unknown;
}

static FailureCategory category_from_hms(const std::string& code, const std::unordered_map<std::string, FailureCategory>& prefixes)
{
    for (const auto& kv : prefixes) {
        if (code.find(kv.first) != std::string::npos)
            return kv.second;
    }
    if (code.find("0300") != std::string::npos || code.find("0500") != std::string::npos)
        return FailureCategory::Filament;
    if (code.find("0700") != std::string::npos || code.find("0800") != std::string::npos)
        return FailureCategory::Temperature;
    if (code.find("1200") != std::string::npos)
        return FailureCategory::Adhesion;
    return FailureCategory::Unknown;
}

static void apply_category_fixes_impl(FailureDiagnosis& d)
{
    switch (d.category) {
    case FailureCategory::Adhesion:
        d.likely_causes.push_back("Bed temperature too low for material");
        d.likely_causes.push_back("Build plate not clean or wrong plate type");
        d.recommended_fixes.push_back({ "brim_type", "", "outer_only", "Improve first-layer adhesion" });
        d.recommended_fixes.push_back({ "brim_width", "", "5", "Wider brim for stability" });
        d.recommended_fixes.push_back({ "initial_layer_speed", "", "30", "Slower first layer" });
        break;
    case FailureCategory::Filament:
        d.likely_causes.push_back("AMS mapping mismatch or empty spool");
        d.recommended_fixes.push_back({ "retraction_length", "", "+0.5", "Slightly increase retraction" });
        break;
    case FailureCategory::Temperature:
        d.likely_causes.push_back("Nozzle or bed temperature out of range");
        d.recommended_fixes.push_back({ "nozzle_temperature", "", "+5", "Raise nozzle temperature slightly" });
        d.recommended_fixes.push_back({ "bed_temperature", "", "+5", "Raise bed temperature slightly" });
        break;
    case FailureCategory::Mechanical:
        d.likely_causes.push_back("Obstruction or belt tension issue");
        d.recommended_fixes.push_back({ "initial_layer_speed", "", "25", "Reduce speed after mechanical recovery" });
        break;
    case FailureCategory::Network:
        d.likely_causes.push_back("LAN or cloud connection interrupted");
        d.description = d.description.empty() ? "Network-related print interruption." : d.description;
        d.recommended_fixes.push_back({ "initial_layer_speed", "", "30", "Use conservative speed after reconnect" });
        break;
    case FailureCategory::UserCancelled:
        d.likely_causes.push_back("Print stopped by user");
        d.description = d.description.empty() ? "The job was cancelled before completion." : d.description;
        break;
    default:
        if (d.title.empty())
            d.title = "Unknown print failure";
        if (d.description.empty())
            d.description = "No specific Bambu error mapping; applied general heuristics.";
        d.likely_causes.push_back("Review printer HMS panel and recent changes");
        break;
    }
}

struct McErrorEntry {
    FailureCategory category;
    std::string title;
    std::string description;
    std::string title_ko;
    std::string description_ko;
    std::string action_line;
    std::string action_line_ko;
};

struct CatalogData {
    std::unordered_map<int, McErrorEntry> mc_errors;
    std::unordered_map<std::string, FailureCategory> hms_prefixes;
};

static void merge_catalog_json(CatalogData& data, const fs::path& p)
{
    if (!fs::exists(p))
        return;
    try {
        boost::nowide::ifstream ifs(p.string());
        json root;
        ifs >> root;
        if (root.contains("mc_errors")) {
            for (auto it = root["mc_errors"].begin(); it != root["mc_errors"].end(); ++it) {
                const int code = std::stoi(it.key());
                const json& v = it.value();
                McErrorEntry e;
                e.category = category_from_string(v.value("category", "unknown"));
                e.title = v.value("title", "");
                e.description = v.value("description", "");
                e.title_ko = v.value("title_ko", "");
                e.description_ko = v.value("description_ko", "");
                e.action_line = v.value("action", "");
                e.action_line_ko = v.value("action_ko", "");
                data.mc_errors[code] = std::move(e);
            }
        }
        if (root.contains("hms_prefixes")) {
            for (auto it = root["hms_prefixes"].begin(); it != root["hms_prefixes"].end(); ++it)
                data.hms_prefixes[it.key()] = category_from_string(it.value().get<std::string>());
        }
    } catch (...) {}
}

static bool& prefer_korean_ui()
{
    static bool v = false;
    return v;
}

void BambuErrorCatalog::set_prefer_korean_ui(bool prefer_ko)
{
    prefer_korean_ui() = prefer_ko;
}

static void localize_diagnosis(FailureDiagnosis& d, const McErrorEntry& e)
{
    if (prefer_korean_ui() && !e.title_ko.empty())
        d.title = e.title_ko;
    else if (!e.title.empty())
        d.title = e.title;
    if (prefer_korean_ui() && !e.description_ko.empty())
        d.description = e.description_ko;
    else if (!e.description.empty())
        d.description = e.description;
    if (prefer_korean_ui() && !e.action_line_ko.empty())
        d.action_line = e.action_line_ko;
    else if (!e.action_line.empty())
        d.action_line = e.action_line;
}

static void fill_default_action_line(FailureDiagnosis& d)
{
    if (!d.action_line.empty())
        return;
    switch (d.category) {
    case FailureCategory::Adhesion:
        d.action_line = prefer_korean_ui()
            ? "베드 온도·브림을 확인한 뒤 Reprint"
            : "Check bed temp and brim, then Reprint";
        break;
    case FailureCategory::Filament:
        d.action_line = prefer_korean_ui()
            ? "AMS 슬롯·필라멘트를 확인한 뒤 Reprint"
            : "Check AMS slot and filament, then Reprint";
        break;
    case FailureCategory::Temperature:
        d.action_line = prefer_korean_ui()
            ? "노즐·베드 온도를 확인한 뒤 Reprint"
            : "Verify nozzle and bed temps, then Reprint";
        break;
  case FailureCategory::Network:
        d.action_line = prefer_korean_ui()
            ? "네트워크 연결 후 Reprint"
            : "Restore network connection, then Reprint";
        break;
    default:
        d.action_line = prefer_korean_ui()
            ? "조치 후 Reprint"
            : "Apply fixes, then Reprint";
        break;
    }
}

static CatalogData& catalog_data()
{
    static CatalogData data;
    static std::once_flag once;
    std::call_once(once, []() {
        data.mc_errors[50348044] = { FailureCategory::Filament, "Filament runout",
            "The printer detected that filament ran out during printing.",
            "필라멘트 소진", "인쇄 중 필라멘트가 떨어졌습니다.",
            "Reload AMS slot and Reprint", "AMS 슬롯을 확인하고 Reprint" };
        data.mc_errors[50364417] = { FailureCategory::Adhesion, "First layer issue",
            "The first layer may have failed to adhere to the build plate.",
            "첫 층 부착 문제", "첫 층이 베드에 잘 붙지 않았을 수 있습니다.",
            "Clean plate, add brim, Reprint", "베드 청소·브림 추가 후 Reprint" };
        data.mc_errors[50364418] = { FailureCategory::Adhesion, "Print detached",
            "The print may have detached from the build plate during printing.",
            "출력물 분리", "인쇄 중 베드에서 떨어졌을 수 있습니다.",
            "Check adhesion settings, Reprint", "부착 설정 확인 후 Reprint" };
        data.mc_errors[50331648] = { FailureCategory::Gcode, "G-code error",
            "The printer reported an error while executing G-code.",
            "G-code 오류", "G-code 실행 중 오류가 발생했습니다.",
            "Re-slice and Reprint", "다시 슬라이스 후 Reprint" };
        data.mc_errors[50397184] = { FailureCategory::Mechanical, "Motion error",
            "A motion or homing related error occurred.",
            "동작 오류", "이동 또는 홈잉 오류가 발생했습니다.",
            "Check mechanics, Reprint", "기계 상태 확인 후 Reprint" };
        data.mc_errors[50348032] = { FailureCategory::Filament, "Filament entangled",
            "Filament may be tangled on the spool or in the AMS path.",
            "필라멘트 엉킴", "스풀 또는 AMS 경로에서 엉켰을 수 있습니다.",
            "Clear AMS path, Reprint", "AMS 경로 정리 후 Reprint" };
        data.mc_errors[50348033] = { FailureCategory::Filament, "Filament stuck",
            "Filament failed to feed — check path and extruder gear.",
            "필라멘트 걸림", "공급 경로와 기어를 확인하세요.",
            "Check feed path, Reprint", "공급 경로 확인 후 Reprint" };
        data.mc_errors[50348049] = { FailureCategory::Filament, "Filament grinding",
            "Extruder may be grinding filament — check tension and temperature.",
            "필라멘트 갈림", "장력과 온도를 확인하세요.",
            "Adjust tension/temp, Reprint", "장력·온도 조정 후 Reprint" };
        data.mc_errors[50348050] = { FailureCategory::Temperature, "Nozzle temperature fault",
            "Nozzle heating failed or dropped out of range during the print.",
            "노즐 온도 오류", "노즐 온도가 범위를 벗어났습니다.",
            "Check hotend, Reprint", "핫엔드 확인 후 Reprint" };
        data.mc_errors[50348051] = { FailureCategory::Temperature, "Bed temperature fault",
            "Heatbed heating failed or dropped out of range during the print.",
            "베드 온도 오류", "히트베드 온도가 범위를 벗어났습니다.",
            "Check heatbed, Reprint", "히트베드 확인 후 Reprint" };
        data.mc_errors[50348052] = { FailureCategory::Mechanical, "Toolhead collision",
            "The toolhead may have collided with the part or build plate.",
            "툴헤드 충돌", "출력물 또는 베드와 충돌했을 수 있습니다.",
            "Remove obstruction, Reprint", "장애물 제거 후 Reprint" };
        data.mc_errors[50348053] = { FailureCategory::UserCancelled, "Print cancelled",
            "The print job was stopped before completion.",
            "인쇄 취소", "사용자가 인쇄를 중단했습니다.",
            "", "" };
        data.mc_errors[50348054] = { FailureCategory::Network, "Cloud / LAN link lost",
            "Connection to the printer or cloud was interrupted during the job.",
            "연결 끊김", "프린터 또는 클라우드 연결이 끊겼습니다.",
            "Restore connection, Reprint", "연결 복구 후 Reprint" };

        merge_catalog_json(data, fs::path(resources_dir()) / "bambu_smart_print" / "error_catalog.json");
        merge_catalog_json(data, fs::path(user_error_catalog_path()));
    });
    return data;
}

void BambuErrorCatalog::reload_user_catalog()
{
    merge_catalog_json(catalog_data(), fs::path(user_error_catalog_path()));
}

FailureDiagnosis BambuErrorCatalog::diagnose(int mc_print_error_code, int print_error, const std::vector<std::string>& hms_codes)
{
    CatalogData& cat = catalog_data();
    FailureDiagnosis d;
    d.confidence = 0.55f;

    auto it = cat.mc_errors.find(mc_print_error_code);
    if (it != cat.mc_errors.end()) {
        d.category = it->second.category;
        localize_diagnosis(d, it->second);
        d.confidence = 0.85f;
    } else if (print_error != 0) {
        d.category    = FailureCategory::Gcode;
        d.title       = "Print error";
        d.description = "The printer reported print_error=" + std::to_string(print_error);
        d.confidence  = 0.65f;
    }

    for (const std::string& hms : hms_codes) {
        FailureCategory hc = category_from_hms(hms, cat.hms_prefixes);
        if (hc != FailureCategory::Unknown && d.category == FailureCategory::Unknown)
            d.category = hc;
        d.likely_causes.push_back("HMS: " + hms);
    }

    apply_category_fixes_impl(d);
    fill_default_action_line(d);
    return d;
}

void BambuErrorCatalog::apply_category_fixes(FailureDiagnosis& d)
{
    apply_category_fixes_impl(d);
}

} // namespace BambuSmartPrint
} // namespace Slic3r
