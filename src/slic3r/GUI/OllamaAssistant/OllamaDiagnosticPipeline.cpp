#include "OllamaDiagnosticPipeline.hpp"

#include "BambuLabWikiSearch.hpp"
#include "OllamaActionJsonExtract.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaRequestRouter.hpp"
#include "OllamaSettingSearch.hpp"

#include "../GUI_App.hpp"

#include "libslic3r/PresetBundle.hpp"

#include <boost/algorithm/string.hpp>

#include <cmath>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

std::vector<std::string> strings_from_json_array(const nlohmann::json& arr)
{
    std::vector<std::string> out;
    if (!arr.is_array())
        return out;
    for (const auto& v : arr) {
        if (v.is_string()) {
            const std::string s = boost::trim_copy(v.get<std::string>());
            if (!s.empty())
                out.push_back(s);
        }
    }
    return out;
}

std::string assess_key(const std::string& key, const std::string& current, const OllamaDiagnosis& diagnosis,
                       bool korean)
{
    if (current.empty() || current == "null")
        return korean ? "현재 값 없음 — 기본 프리셋 확인 필요" : "No current value — check preset";

    if (key == "retraction_length") {
        try {
            const double mm = std::stod(current);
            if (mm < 0.6)
                return korean ? "리트랙션이 낮은 편 — 실링 개선 여지 있음" : "Retraction is low — room to reduce stringing";
            if (mm > 2.0)
                return korean ? "리트랙션이 높음 — 막힘 위험, 온도·속도도 확인" : "Retraction high — clog risk; check temp/speed";
        } catch (...) {
        }
    }

    if (key == "sparse_infill_density") {
        int pct = 0;
        if (current.find('%') != std::string::npos)
            pct = static_cast<int>(std::lround(std::stod(current)));
        if (pct > 0 && pct < 18)
            return korean ? "채움이 낮음 — 강도 문제와 연관 가능" : "Infill is low — may relate to strength issue";
    }

    if (key == "enable_support") {
        if (current == "0" || current == "false")
            return korean ? "서포트 꺼짐 — 오버행 증상이면 켜는 것 검토" : "Supports off — consider enabling for overhangs";
    }

    if (key == "brim_width") {
        try {
            const double w = std::stod(current);
            if (w < 0.1)
                return korean ? "브림 없음 — 접착 문제면 추가 검토" : "No brim — consider for adhesion issues";
        } catch (...) {
        }
    }

    if (key == "layer_height") {
        try {
            const double h = std::stod(current);
            if (h > 0.28)
                return korean ? "레이어가 두꺼움 — 표면·오버행에 영향" : "Layer height is thick — affects surface/overhangs";
        } catch (...) {
        }
    }

    return korean ? "진단과 대조해 조정 방향 결정" : "Compare with diagnosis to pick direction";
}

} // namespace

bool OllamaDiagnosticPipeline::needs_pipeline(const std::string& user_request, bool apply_mode)
{
    if (!apply_mode || user_request.empty())
        return false;
    return OllamaRequestRouter::classify(user_request) != OllamaRequestRoute::Fast;
}

OllamaDiagnosis OllamaDiagnosticPipeline::parse_diagnosis(const std::string& llm_text)
{
    OllamaDiagnosis out;
    try {
        out.raw = extract_ollama_action_json_with_repair(llm_text);
    } catch (...) {
        out.raw = nlohmann::json::object();
    }

    if (out.raw.contains("symptom") && out.raw["symptom"].is_string())
        out.symptom = out.raw["symptom"].get<std::string>();
    if (out.raw.contains("diagnosis") && out.raw["diagnosis"].is_string())
        out.diagnosis = out.raw["diagnosis"].get<std::string>();
    if (out.raw.contains("root_cause") && out.raw["root_cause"].is_string() && out.diagnosis.empty())
        out.diagnosis = out.raw["root_cause"].get<std::string>();
    if (out.raw.contains("message") && out.raw["message"].is_string())
        out.user_message = out.raw["message"].get<std::string>();

    out.likely_causes  = strings_from_json_array(out.raw.value("likely_causes", nlohmann::json::array()));
    out.wiki_queries   = strings_from_json_array(out.raw.value("wiki_search_queries", nlohmann::json::array()));
    if (out.wiki_queries.empty())
        out.wiki_queries = strings_from_json_array(out.raw.value("wiki_queries", nlohmann::json::array()));
    out.candidate_keys = strings_from_json_array(out.raw.value("candidate_keys", nlohmann::json::array()));

    return out;
}

nlohmann::json OllamaDiagnosticPipeline::build_wiki_evidence(const OllamaDiagnosis& diagnosis,
                                                             const std::string& user_request, bool korean)
{
    std::vector<std::string> queries = diagnosis.wiki_queries;
    if (queries.empty() && !diagnosis.diagnosis.empty())
        queries.push_back(diagnosis.diagnosis);
    if (queries.empty() && !diagnosis.symptom.empty())
        queries.push_back(diagnosis.symptom);
    if (queries.empty())
        queries.push_back(user_request);

    return BambuLabWikiSearch::build_wiki_context_from_queries(queries, korean, 2, 1400);
}

nlohmann::json OllamaDiagnosticPipeline::analyze_current_settings(const std::vector<std::string>& candidate_keys,
                                                                  const OllamaDiagnosis& diagnosis, bool korean)
{
    nlohmann::json analysis = nlohmann::json::object();

    nlohmann::json step = nlohmann::json::object();
    step["symptom"]     = diagnosis.symptom;
    step["diagnosis"]   = diagnosis.diagnosis;
    step["likely_causes"] = diagnosis.likely_causes;
    analysis["diagnosis_summary"] = step;

    const DynamicPrintConfig* print_cfg    = nullptr;
    const DynamicPrintConfig* filament_cfg = nullptr;
    if (wxTheApp && wxGetApp().preset_bundle) {
        print_cfg    = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
        filament_cfg = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    }

    std::unordered_set<std::string> seen;
    std::vector<std::string>        keys;
    for (const std::string& k : candidate_keys) {
        if (seen.insert(k).second)
            keys.push_back(k);
    }
    if (keys.empty())
        keys = OllamaSettingSearch::candidate_keys_for_request(
            diagnosis.symptom.empty() ? diagnosis.diagnosis : diagnosis.symptom, 3, 8);

    const nlohmann::json catalog = OllamaSettingSearch::lookup(keys, print_cfg, filament_cfg, korean);
    nlohmann::json       entries = nlohmann::json::array();
    for (const auto& e : catalog) {
        if (!e.is_object() || !e.contains("key"))
            continue;
        const std::string key = e.value("key", "");
        std::string       current_str;
        if (e.contains("current") && !e["current"].is_null()) {
            if (e["current"].is_string())
                current_str = e["current"].get<std::string>();
            else
                current_str = e["current"].dump();
        }
        nlohmann::json row;
        row["key"]        = key;
        if (e.contains("current"))
            row["current"] = e["current"];
        else
            row["current"] = nullptr;
        row["value_type"] = e.value("value_type", "");
        row["assessment"] = assess_key(key, current_str, diagnosis, korean);
        entries.push_back(std::move(row));
    }
    analysis["relevant_settings"] = entries;
    analysis["intent_signals"]    = OllamaIntentContext::build_intent_signals_json();

    if (wxTheApp && wxGetApp().preset_bundle) {
        const auto& print = wxGetApp().preset_bundle->prints.get_edited_preset();
        analysis["active_presets"] = {
            {"print", print.name},
            {"filament", wxGetApp().preset_bundle->filaments.get_edited_preset().name},
            {"printer", wxGetApp().preset_bundle->printers.get_edited_preset().name},
        };
    }

    analysis["step"] = korean ? "현재 설정 분석" : "Current settings analysis";
    return analysis;
}

}} // namespace
