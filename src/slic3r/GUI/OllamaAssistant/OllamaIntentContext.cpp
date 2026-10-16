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

nlohmann::json OllamaIntentContext::build_intent_signals_json()
{
    nlohmann::json signals = nlohmann::json::object();

    const int islands = slice_unsupported_islands();
    if (islands > 0)
        signals["unsupported_islands"] = islands;
    signals["supports_enabled"] = print_support_enabled();

    const OllamaSelectionFootprint fp = current_selection_footprint();
    if (fp.valid) {
        signals["selection_footprint_mm"] = {
            {"x", fp.x_mm},
            {"y", fp.y_mm},
            {"z", fp.z_mm},
        };
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

void OllamaIntentContext::refine_set_config_options(nlohmann::json& options, const std::string& /*user_request*/)
{
    if (!options.is_object())
        return;

    OllamaActionExecutor::normalize_set_config_options(options);
}

}} // namespace
