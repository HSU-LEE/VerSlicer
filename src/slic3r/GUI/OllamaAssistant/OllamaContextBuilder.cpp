#include "OllamaContextBuilder.hpp"

#include "OllamaConfig.hpp"
#include "OllamaConfigProposalCache.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaSettingRegistry.hpp"
#include "OllamaTelemetry.hpp"
#include "OllamaUserFlow.hpp"

#include "../AICoach/AICoachApplyDedup.hpp"
#include "../BambuSmartPrint/BambuSmartPrintService.hpp"

#include "libslic3r/BambuSmartPrint/PrintIntentSession.hpp"
#include "libslic3r/BambuSmartPrint/AutoConfigEngine.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <nlohmann/json.hpp>

#include <mutex>
#include <sstream>

#include <wx/menu.h>

namespace Slic3r { namespace GUI {

namespace {

/** 3D prepare canvas — context must not read preview canvas selection. */
GLCanvas3D* view3d_canvas_for_context(Plater* plater)
{
    return plater ? plater->get_view3D_canvas3D() : nullptr;
}

struct ContextCache
{
    std::mutex  mutex;
    std::string signature;
    std::string full_json;
    std::string compact_json;
};

ContextCache& context_cache()
{
    static ContextCache cache;
    return cache;
}

std::string selection_bbox_signature(Plater* plater)
{
    GLCanvas3D* canvas = view3d_canvas_for_context(plater);
    if (!canvas)
        return "none";
    const Selection& sel = canvas->get_selection();
    if (sel.is_empty())
        return "empty";
    const BoundingBoxf3 bb = sel.get_bounding_box();
    std::ostringstream  sig;
    sig << bb.min.x() << ',' << bb.min.y() << ',' << bb.min.z() << '|' << bb.size().x() << ',' << bb.size().y()
        << ',' << bb.size().z();
    return sig.str();
}

std::string build_context_signature()
{
    std::ostringstream sig;
    if (auto* bundle = wxGetApp().preset_bundle) {
        sig << bundle->prints.get_edited_preset().name << '|';
        sig << bundle->filaments.get_edited_preset().name << '|';
        sig << bundle->printers.get_edited_preset().name << '|';
        sig << OllamaSettingRegistry::config_fingerprint(&bundle->prints.get_edited_preset().config) << '|';
        sig << OllamaSettingRegistry::config_fingerprint(&bundle->filaments.get_edited_preset().config) << '|';
        sig << OllamaSettingRegistry::config_fingerprint(&bundle->printers.get_edited_preset().config) << '|';
    }
    if (Plater* plater = wxGetApp().plater()) {
        sig << plater->model().objects.size() << '|';
        sig << plater->get_partplate_list().get_curr_plate_index() << '|';
        if (GLCanvas3D* v3 = view3d_canvas_for_context(plater))
            sig << v3->get_selection().volumes_count() << '|';
        sig << selection_bbox_signature(plater) << '|';
        const auto& slice_a = BambuSmartPrintService::instance().last_slice_analysis();
        if (slice_a.valid) {
            sig << slice_a.overhang_area_ratio << ',' << slice_a.unsupported_islands_count << '|';
        }
        const auto& readiness = BambuSmartPrintService::instance().last_readiness_report();
        if (readiness.score > 0.f)
            sig << readiness.score << '|';
    }
    // Invalidate cached context when the deterministic proposal changes so refreshed
    // intent/config_proposal blocks are re-emitted for the LLM.
    sig << OllamaConfigProposalCache::instance().latest_proposal_id() << '|';
    return sig.str();
}

nlohmann::json build_menu_context_json()
{
    nlohmann::json out = nlohmann::json::array();
    if (!wxGetApp().mainframe)
        return out;
    wxMenuBar* mb = wxGetApp().mainframe->GetMenuBar();
    if (!mb)
        return out;

    const int menu_count = (int)mb->GetMenuCount();
    for (int mi = 0; mi < menu_count; ++mi) {
        wxMenu* menu = mb->GetMenu(mi);
        if (!menu)
            continue;
        const std::string menu_name = mb->GetMenuLabelText(mi).utf8_string();
        if (OllamaContextBuilder::is_calibration_menu_name(menu_name))
            continue;
        nlohmann::json m;
        m["menu"] = menu_name;
        m["items"] = nlohmann::json::array();

        const auto& items = menu->GetMenuItems();
        for (wxMenuItem* it : items) {
            if (!it)
                continue;
            if (it->IsSeparator())
                continue;
            if (it->IsSubMenu()) {
                // Represent submenus by their label; the LLM can open them by calling menu_item with "Export" etc.
                nlohmann::json si;
                si["label"] = it->GetItemLabelText().utf8_string();
                si["submenu"] = true;
                m["items"].push_back(si);
            } else {
                nlohmann::json ii;
                ii["label"] = it->GetItemLabelText().utf8_string();
                m["items"].push_back(ii);
            }
        }
        out.push_back(m);
    }
    return out;
}

void append_printer_capabilities(nlohmann::json& ctx, const DynamicPrintConfig* printer_cfg)
{
    if (!printer_cfg)
        return;
    nlohmann::json caps = nlohmann::json::object();
    for (const char* key : {"printer_model", "nozzle_diameter", "printable_area", "printable_height"}) {
        if (printer_cfg->has(key))
            caps[key] = printer_cfg->opt_serialize(key);
    }
    if (!caps.empty())
        ctx["printer_capabilities"] = caps;
}

void append_slice_and_readiness(nlohmann::json& ctx, Plater* plater)
{
    BambuSmartPrintService::instance().update_plate_assessment_data(plater);
    const auto& slice_a = BambuSmartPrintService::instance().last_slice_analysis();
    if (slice_a.valid) {
        ctx["slice_analysis"] = {
            {"overhang_area_ratio", slice_a.overhang_area_ratio},
            {"unsupported_islands", slice_a.unsupported_islands_count},
        };
    }
    const auto& readiness = BambuSmartPrintService::instance().last_readiness_report();
    if (readiness.score > 0.f)
        ctx["readiness_score"] = readiness.score;
}

// Append the deterministic print-intent, geometry assessment, and (if available) the
// AutoConfigEngine ConfigProposal so the LLM refines a concrete baseline instead of
// inventing settings. Purely additive to the per-turn context object.
void append_intent_and_proposal(nlohmann::json& ctx, bool ko)
{
    const BambuSmartPrint::PrintIntent intent = BambuSmartPrint::PrintIntentSession::instance().intent();
    ctx["print_intent"] = intent.to_json();

    const auto& mesh = BambuSmartPrintService::instance().last_mesh_analysis();
    nlohmann::json geo = nlohmann::json::object();
    geo["height_mm"]           = mesh.height_mm;
    geo["max_xy_mm"]           = mesh.max_xy_mm;
    geo["overhang_face_ratio"] = mesh.overhang_face_ratio;
    geo["needs_brim"]          = mesh.needs_brim;
    geo["tall_narrow"]         = mesh.tall_narrow;
    geo["is_small_part"]       = mesh.is_small_part;
    geo["fits_bed"]            = mesh.fits_bed;
    geo["complexity_score"]    = mesh.complexity_score;
    if (!mesh.suggested_material.empty())
        geo["suggested_material"] = mesh.suggested_material;
    if (!mesh.suggested_orientation_hint.empty())
        geo["orientation_hint"] = mesh.suggested_orientation_hint;
    ctx["geometry_assessment"] = std::move(geo);

    if (auto proposal = OllamaConfigProposalCache::instance().latest())
        ctx["config_proposal"] = BambuSmartPrint::AutoConfigEngine::proposal_to_context_json(*proposal, ko);
}

nlohmann::json build_plate_objects_json(Plater* plater)
{
    nlohmann::json arr = nlohmann::json::array();
    if (!plater)
        return arr;
    GLCanvas3D* canvas = view3d_canvas_for_context(plater);
    if (!canvas)
        return arr;
    const Selection& sel = canvas->get_selection();
    Model&           model = plater->model();
    PartPlateList&   ppl   = plater->get_partplate_list();
    const int        plate = ppl.get_curr_plate_index();

    auto object_selected = [&](unsigned int obj_idx) {
        if (sel.is_empty())
            return false;
        for (unsigned int vid : sel.get_volume_idxs()) {
            const GLVolume* v = sel.get_volume(vid);
            if (v && static_cast<unsigned int>(v->object_idx()) == obj_idx)
                return true;
        }
        return false;
    };

    for (size_t i = 0; i < model.objects.size(); ++i) {
        ModelObject* obj = model.objects[i];
        if (obj->instances.empty())
            continue;
        const int on_plate = ppl.find_instance_belongs(static_cast<int>(i), 0);
        if (on_plate != plate)
            continue;
        const BoundingBoxf3 bb = obj->instance_bounding_box(0);
        arr.push_back({
            {"object_index", i},
            {"name", obj->name},
            {"plate_index", plate},
            {"selected", object_selected(static_cast<unsigned int>(i))},
            {"size_mm",
             {{"x", bb.size().x()}, {"y", bb.size().y()}, {"z", bb.size().z()}}},
        });
    }
    return arr;
}

nlohmann::json build_context_object(bool compact)
{
    nlohmann::json ctx;
    auto*          bundle  = wxGetApp().preset_bundle;
    Plater*        plater  = wxGetApp().plater();
    const bool     ko      = OllamaContextBuilder::ui_prefers_korean();

    if (bundle) {
        const DynamicPrintConfig& print_cfg = bundle->prints.get_edited_preset().config;
        nlohmann::json          print_opts  = nlohmann::json::object();
        if (compact) {
            static const char* kCompactKeys[] = {
                "layer_height", "sparse_infill_density", "enable_support", "brim_width", "brim_type",
            };
            for (const char* key : kCompactKeys) {
                if (print_cfg.has(key))
                    print_opts[key] = print_cfg.opt_serialize(key);
            }
        } else {
            static const char* kPrintKeys[] = {
                "layer_height", "line_width", "sparse_infill_density", "sparse_infill_pattern",
                "wall_loops", "top_shell_layers", "bottom_shell_layers", "enable_support",
                "brim_width", "brim_type", "outer_wall_speed", "sparse_infill_speed", "initial_layer_print_height",
            };
            for (const char* key : kPrintKeys) {
                if (print_cfg.has(key))
                    print_opts[key] = print_cfg.opt_serialize(key);
            }
        }
        ctx["print_preset"]    = bundle->prints.get_edited_preset().name;
        ctx["print_options"]   = print_opts;
        ctx["filament_preset"] = bundle->filaments.get_edited_preset().name;
        ctx["printer_preset"]  = bundle->printers.get_edited_preset().name;
        append_printer_capabilities(ctx, &bundle->printers.get_edited_preset().config);
    }

    if (plater) {
        ctx["current_plate_index"] = plater->get_partplate_list().get_curr_plate_index();
        ctx["plate_objects"]       = build_plate_objects_json(plater);
        if (GLCanvas3D* v3 = view3d_canvas_for_context(plater)) {
            const Selection& sel = v3->get_selection();
            ctx["selection_count"] = sel.volumes_count();
            ctx["has_selection"]   = !sel.is_empty();
            if (!sel.is_empty()) {
                const BoundingBoxf3 bb = sel.get_bounding_box();
                ctx["selection_size_mm"] = {
                    {"x", bb.size().x()},
                    {"y", bb.size().y()},
                    {"z", bb.size().z()},
                };
                const int obj_idx = sel.get_object_idx();
                if (obj_idx >= 0)
                    ctx["selected_object_index"] = obj_idx;
            }
        }
    }

    if (!compact) {
        if (ollama_auto_catalog_enabled()) {
            ctx["setting_index"] = OllamaSettingRegistry::build_setting_index(3);
            ctx["allowed_config_keys"] = OllamaSettingRegistry::allowed_keys_json();
        }
        if (bundle)
            ctx["setting_catalog"] = OllamaSettingRegistry::build_catalog(&bundle->prints.get_edited_preset().config, ko);
        else
            ctx["setting_catalog"] = OllamaSettingRegistry::build_catalog(nullptr, ko);
        ctx["audience"]            = "intermediate";
        ctx["setting_rules"]       = ko
            ? "결과 중심: current 값 기준 최소 변경. pro_tips·모델 지식·setting_catalog(고급 키 포함) 활용. 키는 catalog에 있을 때만."
            : "Outcome-first: use pro_tips, your 3D printing knowledge, and setting_catalog (incl. advanced keys). Catalog keys only.";
        nlohmann::json hints = nlohmann::json::object();
        if (ko) {
            hints["bed_adhesion"] =
                "안 붙음/들뜸: 바닥 보조 테두리(브림) 또는 첫 층 — 접착 문제일 때.";
            hints["overhang"]     = "공중/매달림: 받침 구조 또는 눕히기 — 오버행 문제일 때.";
            hints["strength"]     = "부서짐/약함: 안쪽 채움·벽 두께 — 구조 문제일 때 (접착과 다름).";
            hints["warp"]         = "모서리 들뜸: 접착·베드·브림.";
            hints["stringing"]    = "실 늘어짐: 리트랙션·온도.";
            hints["speed"]        = "느림: 레이어 두께·채움 — 품질 tradeoff 설명.";
            hints["surface"]      = "거친 표면: 레이어 두께 감소.";
        } else {
            hints["bed_adhesion"] = "Won't stick: helper ring at bottom or first layer — adhesion issue.";
            hints["overhang"]     = "Mid-air print: supports or lay flat — overhang issue.";
            hints["strength"]     = "Breaks easily: tighter fill or thicker walls — structural, not brim.";
            hints["warp"]         = "Corner lift: adhesion, bed, brim.";
            hints["stringing"]    = "Stringing: retraction, temperature.";
            hints["speed"]        = "Too slow: layer height, infill — mention quality tradeoff.";
            hints["surface"]      = "Rough surface: lower layer height.";
        }
        ctx["plain_language_hints"] = hints;
        const nlohmann::json menu_ctx = build_menu_context_json();
        if (!menu_ctx.empty())
            ctx["menu_catalog"] = menu_ctx;
    } else {
        ctx["plain_language_hints"] = ko
            ? nlohmann::json{{"bed_adhesion", "접착"}, {"overhang", "오버행"}, {"strength", "강도(채움·벽)"},
                             {"speed", "속도"}, {"surface", "표면"}}
            : nlohmann::json{{"bed_adhesion", "adhesion"}, {"overhang", "overhang"}, {"strength", "strength"},
                             {"speed", "speed"}, {"surface", "surface"}};
    }

    ctx["engineering_hints"] = OllamaIntentContext::build_engineering_hints_json();
    ctx["user_flow"]         = OllamaUserFlow::build_flow_context_json();

    append_slice_and_readiness(ctx, plater);
    append_intent_and_proposal(ctx, ko);

    OllamaIntentContext::consume_slice_feedback_if_ready();
    ctx["intent_signals"] = OllamaIntentContext::build_intent_signals_json();
    OllamaIntentContext::refresh_cached_intent_signals();

    return ctx;
}

} // namespace

bool OllamaContextBuilder::ui_prefers_korean()
{
    const wxString code = GUI::wxGetApp().current_language_code();
    wxString lang = code.BeforeFirst('_').BeforeFirst('-').Lower();
    if (lang == wxString("ko"))
        return true;
    lang = GUI::wxGetApp().current_language_code_safe().BeforeFirst('_').Lower();
    return lang == wxString("ko");
}

bool OllamaContextBuilder::is_calibration_menu_name(const std::string& name)
{
    return boost::icontains(name, "calib") || name.find("캘리브") != std::string::npos ||
           name.find("보정") != std::string::npos;
}

void OllamaContextBuilder::invalidate_context_cache()
{
    auto& cache = context_cache();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.signature.clear();
        cache.full_json.clear();
        cache.compact_json.clear();
    }
    OllamaTelemetry::context_cache_invalidate();
}

void OllamaContextBuilder::notify_plater_context_changed(bool clear_coach_dedup)
{
    invalidate_context_cache();
    OllamaIntentContext::refresh_cached_intent_signals();
    if (clear_coach_dedup)
        AICoachApplyDedup::instance().clear();
}

std::string OllamaContextBuilder::fit_context_json_to_limit(std::string json, size_t max_chars)
{
    if (json.size() <= max_chars)
        return json;
    try {
        nlohmann::json ctx = nlohmann::json::parse(json);
        auto drop_lowest_priority_catalog_entry = [&]() -> bool {
            if (!ctx.contains("setting_catalog") || !ctx["setting_catalog"].is_array()
                || ctx["setting_catalog"].empty())
                return false;
            auto& catalog = ctx["setting_catalog"];
            size_t drop_idx = 0;
            int    lowest   = 1000;
            for (size_t i = 0; i < catalog.size(); ++i) {
                if (!catalog[i].is_object() || !catalog[i].contains("key") || !catalog[i]["key"].is_string())
                    continue;
                const std::string key = catalog[i]["key"].get<std::string>();
                int               pri = 50;
                if (const OllamaSettingSpec* sp = OllamaSettingRegistry::find_spec(key))
                    pri = sp->context_priority;
                if (pri < lowest) {
                    lowest   = pri;
                    drop_idx = i;
                }
            }
            catalog.erase(drop_idx);
            return true;
        };

        json = ctx.dump(2);
        while (json.size() > max_chars && drop_lowest_priority_catalog_entry())
            json = ctx.dump(2);

        if (json.size() > max_chars && ctx.contains("setting_index") && ctx["setting_index"].is_array()
            && ctx["setting_index"].size() > 40) {
            auto& idx = ctx["setting_index"];
            idx.erase(idx.begin() + idx.size() / 2, idx.end());
            json = ctx.dump(2);
        }

        if (json.size() > max_chars && ctx.contains("menu_catalog"))
            ctx.erase("menu_catalog");
        if (json.size() > max_chars && ctx.contains("plain_language_hints"))
            ctx.erase("plain_language_hints");
        if (json.size() > max_chars && ctx.contains("pro_tips") && ctx["pro_tips"].is_array()
            && ctx["pro_tips"].size() > 2) {
            auto& tips = ctx["pro_tips"];
            tips.erase(tips.begin() + tips.size() / 2, tips.end());
        }
        json = ctx.dump(2);
        if (json.size() > max_chars) {
            const std::string compact = build_compact_context_json();
            if (compact.size() <= max_chars)
                return compact;
            json = compact;
        }
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "Ollama executor: context-fit trimming failed; sending untrimmed context";
    }
    if (json.size() > max_chars) {
        const size_t cut = json.rfind('}', max_chars);
        if (cut != std::string::npos && cut > max_chars / 2)
            json = json.substr(0, cut + 1);
        else
            json = json.substr(0, max_chars);
    }
    return json;
}

std::string OllamaContextBuilder::build_context_json()
{
    auto& cache = context_cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    const std::string sig = build_context_signature();
    if (sig != cache.signature) {
        cache.signature.clear();
        cache.full_json.clear();
        cache.compact_json.clear();
    }
    if (cache.full_json.empty()) {
        cache.signature  = sig;
        cache.full_json  = build_context_object(/*compact*/ false).dump(2);
    }
    return cache.full_json;
}

std::string OllamaContextBuilder::build_compact_context_json()
{
    auto& cache = context_cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    const std::string sig = build_context_signature();
    if (sig != cache.signature) {
        cache.signature.clear();
        cache.full_json.clear();
        cache.compact_json.clear();
    }
    if (cache.compact_json.empty()) {
        cache.signature    = sig;
        cache.compact_json = build_context_object(/*compact*/ true).dump(2);
    }
    return cache.compact_json;
}

}} // namespace
