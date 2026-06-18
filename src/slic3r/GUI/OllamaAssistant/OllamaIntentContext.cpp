#include "OllamaIntentContext.hpp"

#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaTelemetry.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Selection.hpp"

#include "libslic3r/PresetBundle.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <mutex>

namespace Slic3r { namespace GUI {

namespace {

std::mutex              s_signals_mutex;
nlohmann::json          s_cached_intent_signals = nlohmann::json::object();
std::atomic<bool>       s_pending_slice_feedback{false};

bool print_option_truthy(const DynamicPrintConfig* cfg, const char* key)
{
    if (!cfg || !cfg->has(key))
        return false;
    try {
        const std::string v = cfg->opt_serialize(key);
        return v == "1" || v == "true";
    } catch (...) {
        return false;
    }
}

} // namespace

OllamaSelectionFootprint OllamaIntentContext::current_selection_footprint()
{
    OllamaSelectionFootprint fp;
    Plater*                  plater = wxGetApp().plater();
    if (!plater)
        return fp;
    GLCanvas3D* canvas = plater->get_view3D_canvas3D();
    if (!canvas)
        return fp;
    const Selection& sel = canvas->get_selection();
    if (sel.is_empty())
        return fp;
    const BoundingBoxf3 bb = sel.get_bounding_box();
    fp.x_mm                = bb.size().x();
    fp.y_mm                = bb.size().y();
    fp.z_mm                = bb.size().z();
    fp.valid               = fp.x_mm > 0.0 && fp.y_mm > 0.0;
    return fp;
}

bool OllamaIntentContext::selection_is_tall_narrow(double ratio)
{
    const OllamaSelectionFootprint fp = current_selection_footprint();
    if (!fp.valid)
        return false;
    return ollama_selection_is_tall_narrow(fp.x_mm, fp.y_mm, fp.z_mm, ratio);
}

bool OllamaIntentContext::slice_suggests_support(double min_overhang_ratio)
{
    const auto& slice_a = BambuSmartPrintService::instance().last_slice_analysis();
    return slice_a.valid && slice_a.overhang_area_ratio >= static_cast<float>(min_overhang_ratio);
}

int OllamaIntentContext::slice_unsupported_islands()
{
    const auto& slice_a = BambuSmartPrintService::instance().last_slice_analysis();
    return slice_a.valid ? slice_a.unsupported_islands_count : 0;
}

bool OllamaIntentContext::print_support_enabled()
{
    if (auto* bundle = wxGetApp().preset_bundle)
        return print_option_truthy(&bundle->prints.get_edited_preset().config, "enable_support");
    return false;
}

double OllamaIntentContext::recommended_brim_width_mm()
{
    const OllamaSelectionFootprint fp = current_selection_footprint();
    if (!fp.valid)
        return 5.0;
    return ollama_recommended_brim_width_mm(fp.x_mm, fp.y_mm);
}

bool OllamaIntentContext::user_vague_fix_request(const std::string& user)
{
    return user.find("고쳐") != std::string::npos || user.find("도와") != std::string::npos ||
           user.find("help") != std::string::npos || user.find("fix") != std::string::npos ||
           user.find("문제") != std::string::npos || user.find("안돼") != std::string::npos ||
           user.find("안되") != std::string::npos || user.find("망했") != std::string::npos ||
           user.find("failed") != std::string::npos;
}

bool OllamaIntentContext::user_wants_warp_relief(const std::string& user)
{
    return user.find("들뜸") != std::string::npos || user.find("warp") != std::string::npos ||
           user.find("curl") != std::string::npos || user.find("lift") != std::string::npos ||
           user.find("코너") != std::string::npos || user.find("corner") != std::string::npos;
}

bool OllamaIntentContext::user_wants_stringing_relief(const std::string& user)
{
    return user.find("stringing") != std::string::npos || user.find("string") != std::string::npos ||
           user.find("실") != std::string::npos || user.find("거미") != std::string::npos ||
           user.find("spider") != std::string::npos || user.find("ooze") != std::string::npos;
}

bool OllamaIntentContext::user_wants_top_surface_quality(const std::string& user)
{
    return user.find("윗면") != std::string::npos || user.find("윗 층") != std::string::npos ||
           user.find("top surface") != std::string::npos || user.find("rough top") != std::string::npos ||
           user.find("거칠") != std::string::npos || user.find("ironing") != std::string::npos ||
           user.find("아이어링") != std::string::npos;
}

bool OllamaIntentContext::user_wants_first_layer_help(const std::string& user)
{
    return user.find("첫층") != std::string::npos || user.find("첫 층") != std::string::npos ||
           user.find("first layer") != std::string::npos || user.find("initial layer") != std::string::npos;
}

bool OllamaIntentContext::user_wants_lay_flat(const std::string& user)
{
    return user.find("눕혀") != std::string::npos || user.find("눕") != std::string::npos ||
           user.find("lay flat") != std::string::npos || user.find("넘어") != std::string::npos ||
           user.find("쓰러") != std::string::npos || user.find("기둥") != std::string::npos ||
           user.find("tall") != std::string::npos || user.find("narrow") != std::string::npos;
}

nlohmann::json OllamaIntentContext::build_intent_signals_json()
{
    nlohmann::json signals = nlohmann::json::object();

    if (slice_suggests_support())
        signals["support_recommended"] = true;
    const int islands = slice_unsupported_islands();
    if (islands > 0) {
        signals["unsupported_islands"] = islands;
        signals["still_needs_support"] = true;
    }
    if (slice_suggests_support() && !print_support_enabled())
        signals["support_recommended_after_slice"] = true;

    const OllamaSelectionFootprint fp = current_selection_footprint();
    signals["recommended_brim_width_mm"] = ollama_recommended_brim_width_mm(fp.x_mm, fp.y_mm);

    if (fp.valid) {
        signals["selection_footprint_mm"] = {
            {"x", fp.x_mm},
            {"y", fp.y_mm},
            {"z", fp.z_mm},
        };
        if (ollama_selection_is_tall_narrow(fp.x_mm, fp.y_mm, fp.z_mm))
            signals["lay_flat_recommended"] = true;
    }

    if (auto* bundle = wxGetApp().preset_bundle)
        signals["active_filament_count"] = bundle->filament_presets.size();

    return signals;
}

nlohmann::json OllamaIntentContext::build_engineering_hints_json()
{
    nlohmann::json hints = nlohmann::json::object();
    hints["minimal_change_max_keys"] = 2;
    hints["principle"] =
        "Infer the user's desired outcome and root cause before changing settings; prefer relative changes from current.";

    nlohmann::json priority = nlohmann::json::object();
    priority["adhesion"]  = nlohmann::json::array({"brim_width", "initial_layer_print_height"});
    priority["strength"]  = nlohmann::json::array({"sparse_infill_density", "wall_loops"});
    priority["overhang"]  = nlohmann::json::array({"enable_support"});
    priority["speed"]     = nlohmann::json::array({"layer_height", "sparse_infill_density"});
    priority["surface"]   = nlohmann::json::array({"layer_height", "top_shell_layers"});
    priority["stringing"] = nlohmann::json::array({"retraction_length"});
    hints["symptom_key_priority"] = priority;

    if (auto* bundle = wxGetApp().preset_bundle) {
        const DynamicPrintConfig& cfg = bundle->prints.get_edited_preset().config;
        if (cfg.has("sparse_infill_density"))
            hints["current_sparse_infill"] = cfg.opt_serialize("sparse_infill_density");
        if (cfg.has("wall_loops"))
            hints["current_wall_loops"] = cfg.opt_serialize("wall_loops");
        if (cfg.has("brim_width"))
            hints["current_brim_width_mm"] = cfg.opt_serialize("brim_width");
        if (cfg.has("layer_height"))
            hints["current_layer_height_mm"] = cfg.opt_serialize("layer_height");
        if (cfg.has("enable_support"))
            hints["supports_enabled"] = cfg.opt_serialize("enable_support") == "1";
    }

    return hints;
}

void OllamaIntentContext::refresh_cached_intent_signals()
{
    nlohmann::json built = build_intent_signals_json();
    std::lock_guard<std::mutex> lock(s_signals_mutex);
    s_cached_intent_signals = std::move(built);
}

nlohmann::json OllamaIntentContext::cached_intent_signals_json()
{
    std::lock_guard<std::mutex> lock(s_signals_mutex);
    return s_cached_intent_signals;
}

void OllamaIntentContext::mark_pending_slice_feedback()
{
    s_pending_slice_feedback.store(true);
}

void OllamaIntentContext::consume_slice_feedback_if_ready()
{
    if (!s_pending_slice_feedback.exchange(false))
        return;
    Plater* plater = wxGetApp().plater();
    if (plater)
        BambuSmartPrintService::instance().update_plate_assessment_data(plater);
    refresh_cached_intent_signals();
    const nlohmann::json sig = cached_intent_signals_json();
    const bool           still = sig.value("still_needs_support", false);
    const int            isl   = sig.value("unsupported_islands", 0);
    OllamaTelemetry::slice_feedback_evaluated(still, isl);
}

static bool user_describes_durability(const std::string& user)
{
    return user.find("파손") != std::string::npos || user.find("부서") != std::string::npos ||
           user.find("부러") != std::string::npos || user.find("fragile") != std::string::npos ||
           user.find("brittle") != std::string::npos || user.find("break") != std::string::npos;
}

void OllamaIntentContext::refine_set_config_options(nlohmann::json& options, const std::string& user_request)
{
    if (!options.is_object())
        return;

    OllamaActionExecutor::normalize_set_config_options(options);

    const bool has_brim_key = options.contains("brim_width") || options.contains("enable_brim");
    if (has_brim_key) {
        const double rec = recommended_brim_width_mm();
        if (options.contains("brim_width") && options["brim_width"].is_number()) {
            const double w = options["brim_width"].get<double>();
            if (w > 0.0 && w == 5.0)
                options["brim_width"] = rec;
        } else if (options.contains("enable_brim") && !options.contains("brim_width")) {
            options["brim_width"] = rec;
            if (!options.contains("brim_type"))
                options["brim_type"] = "outer_only";
        }
    }

    if (user_describes_durability(user_request)) {
        if (!options.contains("wall_loops") && !options.contains("sparse_infill_density"))
            options["wall_loops"] = 3;
        else if (has_brim_key && !options.contains("wall_loops"))
            options["wall_loops"] = 3;
    }

    if (user_wants_warp_relief(user_request) && !options.contains("elefant_foot_compensation"))
        options["elefant_foot_compensation"] = 0.1;

    if (user_wants_stringing_relief(user_request) && !options.contains("retraction_length"))
        options["retraction_length"] = 0.8;

    if (user_wants_top_surface_quality(user_request)) {
        if (!options.contains("ironing_type"))
            options["ironing_type"] = "top";
        if (!options.contains("top_shell_layers"))
            options["top_shell_layers"] = 4;
    }

    if (user_wants_first_layer_help(user_request) && !options.contains("initial_layer_print_height"))
        options["initial_layer_print_height"] = 0.24;
}

}} // namespace
