#include "OllamaMeshOps.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaActionJsonExtract.hpp"
#include "OllamaActionValidator.hpp"
#include "OllamaConfig.hpp"
#include "OllamaContextBuilder.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaMakerWorldActions.hpp"
#include "OllamaSettingRegistry.hpp"
#include "OllamaSettingSearch.hpp"
#include "OllamaSystemPrompts.hpp"
#include "OllamaPrintingTips.hpp"
#include "OllamaResponseNormalizer.hpp"
#include "OllamaUserFlow.hpp"
#include "OllamaAgentStateService.hpp"
#include "BambuLabWikiSearch.hpp"
#include "OllamaTelemetry.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLToolbar.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <chrono>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <wx/filefn.h>
#include <wx/glcanvas.h>
#include <wx/filename.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr double kPi = 3.14159265358979323846;

double normalize_rotation_degrees(double deg)
{
    while (deg > 180.0)
        deg -= 360.0;
    while (deg <= -180.0)
        deg += 360.0;
    return deg;
}

bool rotation_degrees_equivalent(double a, double b, double tolerance = 0.5)
{
    return std::abs(normalize_rotation_degrees(a - b)) <= tolerance;
}

std::optional<double> selection_instance_z_degrees(const Selection& sel)
{
    if (sel.is_empty())
        return std::nullopt;
    const GLVolume* vol = sel.get_first_volume();
    if (!vol)
        return std::nullopt;
    return normalize_rotation_degrees(vol->get_instance_rotation(Z) * 180.0 / kPi);
}

/** 3D prepare canvas — object transforms must not use preview canvas selection. */
GLCanvas3D* view3d_canvas_for_object_ops(Plater* plater)
{
    return plater ? plater->get_view3D_canvas3D() : nullptr;
}

std::vector<unsigned int> object_indices_on_current_plate(Plater* plater)
{
    std::vector<unsigned int> out;
    if (!plater)
        return out;
    PartPlateList& ppl = plater->get_partplate_list();
    const int      plate = ppl.get_curr_plate_index();
    Model&         model = plater->model();
    for (size_t i = 0; i < model.objects.size(); ++i) {
        if (model.objects[i]->instances.empty())
            continue;
        const int on_plate = ppl.find_instance_belongs(static_cast<int>(i), 0);
        if (on_plate == plate)
            out.push_back(static_cast<unsigned int>(i));
    }
    return out;
}

bool action_has_object_target(const nlohmann::json& action)
{
    if (action.contains("object_id") && action["object_id"].is_number_integer())
        return true;
    if (action.contains("object_ids") && action["object_ids"].is_array() && !action["object_ids"].empty())
        return true;
    if (action.contains("object_name") && action["object_name"].is_string()
        && !action["object_name"].get<std::string>().empty())
        return true;
    if (action.contains("target") && action["target"].is_string() && !action["target"].get<std::string>().empty())
        return true;
    return false;
}

bool select_objects_for_action(Plater* plater, const nlohmann::json& action)
{
    GLCanvas3D* canvas = view3d_canvas_for_object_ops(plater);
    if (!canvas)
        return false;
    Selection& sel = canvas->get_selection();
    Model&     model = plater->model();

    auto pick_object = [&](unsigned int obj_idx, bool single) {
        if (obj_idx >= model.objects.size())
            return false;
        if (single)
            sel.clear();
        sel.add_object(obj_idx, single);
        return true;
    };

    if (action.contains("object_ids") && action["object_ids"].is_array()) {
        sel.clear();
        bool any = false;
        for (const auto& id : action["object_ids"]) {
            if (!id.is_number_integer())
                continue;
            const unsigned int obj_idx = id.get<unsigned int>();
            if (obj_idx < model.objects.size()) {
                sel.add_object(obj_idx, false);
                any = true;
            }
        }
        return any && !sel.is_empty();
    }

    if (action.contains("object_id") && action["object_id"].is_number_integer()) {
        sel.clear();
        const unsigned int obj_idx = action["object_id"].get<unsigned int>();
        pick_object(obj_idx, true);
        return !sel.is_empty();
    }

    std::string name_hint;
    if (action.contains("object_name") && action["object_name"].is_string())
        name_hint = action["object_name"].get<std::string>();
    else if (action.contains("target") && action["target"].is_string())
        name_hint = action["target"].get<std::string>();
    boost::algorithm::trim(name_hint);
    if (!name_hint.empty()) {
        sel.clear();
        bool any = false;
        for (size_t i = 0; i < model.objects.size(); ++i) {
            const std::string& n = model.objects[i]->name;
            if (boost::icontains(n, name_hint)) {
                sel.add_object(static_cast<unsigned int>(i), false);
                any = true;
            }
        }
        if (any && sel.volumes_count() == 1)
            return !sel.is_empty();
        if (any)
            return !sel.is_empty();
    }

    return false;
}

/** Select plate objects for transforms when nothing is selected (always uses 3D view). */
bool ensure_selection_for_object_ops(Plater* plater, const nlohmann::json* action = nullptr)
{
    if (!plater || plater->model().objects.empty())
        return false;
    GLCanvas3D* canvas = view3d_canvas_for_object_ops(plater);
    if (!canvas)
        return false;

    if (action && action_has_object_target(*action)) {
        if (select_objects_for_action(plater, *action))
            return true;
    }

    Selection& sel = canvas->get_selection();
    if (!sel.is_empty())
        return true;

    const std::vector<unsigned int> on_plate = object_indices_on_current_plate(plater);
    if (on_plate.size() == 1) {
        sel.clear();
        sel.add_object(on_plate.front(), true);
        return !sel.is_empty();
    }

    plater->select_all();
    return !canvas->get_selection().is_empty();
}

static void ensure_prepare_view_for_object_ops()
{
    MainFrame* mf = wxGetApp().mainframe;
    if (mf)
        mf->request_select_tab(MainFrame::tp3DEditor);
}

std::optional<unsigned int> infer_single_geometry_target(Plater* plater)
{
    if (!plater)
        return std::nullopt;
    if (GLCanvas3D* canvas = view3d_canvas_for_object_ops(plater)) {
        const Selection& sel = canvas->get_selection();
        if (!sel.is_empty()) {
            const int obj_idx = sel.get_object_idx();
            if (obj_idx >= 0)
                return static_cast<unsigned int>(obj_idx);
        }
    }
    const std::vector<unsigned int> on_plate = object_indices_on_current_plate(plater);
    if (on_plate.size() == 1)
        return on_plate.front();
    return std::nullopt;
}

static void augment_geometry_object_targets_impl(nlohmann::json& root, const std::string& /*user*/)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return;

    const std::optional<unsigned int> target = infer_single_geometry_target(plater);
    if (!target)
        return;

    for (auto& action : root["actions"]) {
        if (!action.is_object())
            continue;
        const std::string type = action.value("type", "");
        if (type != "rotate" && type != "translate" && type != "scale" && type != "clone_selection"
            && type != "delete_selection" && type != "mesh_boolean" && type != "mirror_mesh"
            && type != "repair_mesh")
            continue;
        if (action_has_object_target(action))
            continue;
        action["object_id"] = *target;
    }
}

Preset::Type preset_type_from_string(const std::string& s)
{
    if (s == "filament")
        return Preset::TYPE_FILAMENT;
    if (s == "printer")
        return Preset::TYPE_PRINTER;
    return Preset::TYPE_PRINT;
}

static std::string normalize_config_key_local(std::string key)
{
    boost::algorithm::trim(key);
    boost::algorithm::to_lower(key);
    if (key == "support_material" || key == "support")
        return "enable_support";
    if (key == "enable_brim" || key == "brim_enable" || key == "brim_on")
        return "enable_brim";
    if (key == "brim")
        return "brim_width";
    return key;
}

static bool json_truthy(const nlohmann::json& v)
{
    if (v.is_boolean())
        return v.get<bool>();
    if (v.is_number())
        return v.get<double>() != 0.0;
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        boost::algorithm::trim(s);
        boost::algorithm::to_lower(s);
        return s == "1" || s == "true" || s == "yes" || s == "on";
    }
    return false;
}

static bool json_brim_width_positive(const nlohmann::json& options)
{
    if (!options.contains("brim_width"))
        return false;
    if (options["brim_width"].is_number())
        return options["brim_width"].get<double>() > 0.0;
    if (options["brim_width"].is_string()) {
        try {
            return std::stod(options["brim_width"].get<std::string>()) > 0.0;
        } catch (...) {
            BOOST_LOG_TRIVIAL(debug) << "Ollama executor: unparsable brim_width string";
        }
    }
    return false;
}

static bool is_auto_brim_type_value(const nlohmann::json& val)
{
    if (!val.is_string())
        return false;
    std::string bt = val.get<std::string>();
    boost::algorithm::to_lower(bt);
    boost::algorithm::trim(bt);
    boost::algorithm::replace_all(bt, " ", "");
    boost::algorithm::replace_all(bt, "-", "_");
    return bt == "auto" || bt == "auto_brim" || bt == "autobrim";
}

/** Maps enable_brim / boolean "brim" to brim_width + brim_type before apply. */
static void expand_brim_options(nlohmann::json& options)
{
    auto apply_brim_on = [&](bool on) {
        if (on) {
            const bool has_width = options.contains("brim_width");
            double           w   = 0.0;
            if (has_width) {
                if (options["brim_width"].is_number())
                    w = options["brim_width"].get<double>();
                else if (options["brim_width"].is_string())
                    try {
                        w = std::stod(options["brim_width"].get<std::string>());
                    } catch (...) {
                        BOOST_LOG_TRIVIAL(debug) << "Ollama executor: unparsable brim_width string";
                    }
            }
            if (!has_width || w <= 0.0)
                options["brim_width"] = 5.0;
            // Explicit "turn on brim" → outer brim, not auto_brim (width-only auto mode).
            options["brim_type"] = "outer_only";
        } else {
            options["brim_width"] = 0.0;
        }
    };

    if (options.contains("enable_brim")) {
        apply_brim_on(json_truthy(options["enable_brim"]));
        options.erase("enable_brim");
    }
    if (options.contains("brim") && (options["brim"].is_boolean() || options["brim"].is_number() ||
                                     (options["brim"].is_string() && json_truthy(options["brim"])))) {
        apply_brim_on(json_truthy(options["brim"]));
        options.erase("brim");
    }
    if (json_brim_width_positive(options)
        && (!options.contains("brim_type") || is_auto_brim_type_value(options["brim_type"])))
        options["brim_type"] = "outer_only";
}

/** Default support mode for assistant-applied settings (Bambu tree supports). */
static constexpr const char* kAssistantDefaultSupportType = "tree(auto)";

/** Maps support_type aliases; normal/hybrid modes become tree(auto) for assistant applies. */
static std::string normalize_support_type_value(std::string v)
{
    boost::algorithm::to_lower(v);
    boost::algorithm::trim(v);
    boost::algorithm::replace_all(v, " ", "");

    if (v == "auto" || v == "normal" || v == "normalauto" || v == "hybrid(auto)" || v == "hybridauto"
        || v == "normal(auto)" || v == "normal(manual)" || v == "normalmanual" || v == "manual")
        return kAssistantDefaultSupportType;
    if (v == "tree" || v == "treeauto" || v == "tree(auto)" || v == "tree(manual)" || v == "treemanual")
        return kAssistantDefaultSupportType;
    return v;
}

static void expand_support_options(nlohmann::json& options)
{
    if (!options.is_object())
        return;

    if (options.contains("enable_support") && !json_truthy(options["enable_support"])) {
        options.erase("support_type");
        return;
    }

    if (options.contains("support_type") && options["support_type"].is_string())
        options["support_type"] = normalize_support_type_value(options["support_type"].get<std::string>());

    if (!options.contains("enable_support"))
        return;
    if (!json_truthy(options["enable_support"]))
        return;

    // Bambu/Orca: tree(auto) generates tree supports from geometry.
    options["support_type"] = kAssistantDefaultSupportType;
}

static void strip_disallowed_config_keys(nlohmann::json& options)
{
    if (!options.is_object())
        return;
    for (auto it = options.begin(); it != options.end();) {
        if (!OllamaActionValidator::is_allowed_config_key(it.key()))
            it = options.erase(it);
        else
            ++it;
    }
}

static void normalize_set_config_options_impl(nlohmann::json& options)
{
    if (!options.is_object())
        return;
    strip_disallowed_config_keys(options);
    expand_brim_options(options);
    expand_support_options(options);
}

static std::string normalize_brim_type(std::string v)
{
    boost::algorithm::to_lower(v);
    boost::algorithm::trim(v);
    boost::algorithm::replace_all(v, " ", "");
    boost::algorithm::replace_all(v, "-", "_");

    if (v == "outer" || v == "outeronly" || v == "outerbrim" || v == "outer_brim")
        return "outer_only";
    if (v == "inner" || v == "inneronly")
        return "inner_only";
    if (v == "auto" || v == "autobrim")
        return "auto_brim";
    if (v == "ears" || v == "mouseear" || v == "brimears")
        return "brim_ears";
    if (v == "no" || v == "nobrim" || v == "off" || v == "none")
        return "no_brim";
    if (v == "painted")
        return "painted";
    if (v == "outerandinner" || v == "both")
        return "outer_and_inner";
    return v;
}

DynamicPrintConfig* edited_config(Preset::Type type, int filament_index = 0)
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return nullptr;
    switch (type) {
    case Preset::TYPE_FILAMENT: {
        const int max_slots = static_cast<int>(bundle->filament_presets.size());
        const int idx       = OllamaIntentContext::clamp_filament_index(filament_index, max_slots);
        if (max_slots <= 0)
            return &bundle->filaments.get_edited_preset().config;
        const std::string& preset_name = bundle->filament_presets[idx];
        if (Preset* preset = bundle->filaments.find_preset(preset_name, false))
            return &preset->config;
        return &bundle->filaments.get_edited_preset().config;
    }
    case Preset::TYPE_PRINTER:
        return &bundle->printers.get_edited_preset().config;
    default:
        return &bundle->prints.get_edited_preset().config;
    }
}

static DynamicPrintConfig* edited_config_for_action(const nlohmann::json& action)
{
    const Preset::Type ptype = preset_type_from_string(action.value("preset", "print"));
    int                fidx  = action.value("filament_index", 0);
    if (!action.contains("filament_index") && ptype == Preset::TYPE_FILAMENT)
        fidx = 0;
    return edited_config(ptype, fidx);
}

static bool is_blocked_calibration_menu_item(const std::string& menu, const std::string& item)
{
    if (OllamaContextBuilder::is_calibration_menu_name(menu))
        return true;
    return boost::icontains(item, "VFA") || item.find("캘리브") != std::string::npos ||
           boost::icontains(item, "calibration");
}

std::string json_value_to_config_string(const nlohmann::json& v)
{
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_boolean())
        return v.get<bool>() ? "1" : "0";
    if (v.is_number_integer())
        return std::to_string(v.get<long long>());
    if (v.is_number_float()) {
        std::ostringstream oss;
        oss << v.get<double>();
        return oss.str();
    }
    return v.dump();
}

static std::string normalize_config_value(std::string v, const std::string& key)
{
    boost::algorithm::trim(v);

    // Common "human" suffixes from STT/LLM.
    auto strip_suffix_ci = [&](const std::string& suffix) {
        if (v.size() >= suffix.size() && boost::iends_with(v, suffix))
            v.resize(v.size() - suffix.size());
    };

    // Remove commas in numbers: "1,000" -> "1000"
    boost::algorithm::replace_all(v, ",", "");

    // mm units are frequently appended by users/LLMs.
    if (key == "layer_height" || key == "line_width" || key == "brim_width" || key == "initial_layer_print_height")
        strip_suffix_ci("mm");

    boost::algorithm::trim(v);

    // Booleans often come as words.
    if (boost::iequals(v, "true") || boost::iequals(v, "yes") || boost::iequals(v, "on"))
        return "1";
    if (boost::iequals(v, "false") || boost::iequals(v, "no") || boost::iequals(v, "off"))
        return "0";

    // For density, a bare number is usually a percent (integer or decimal).
    if (key.find("density") != std::string::npos && v.find('%') == std::string::npos) {
        int  dots   = 0;
        bool numeric = !v.empty();
        for (unsigned char c : v) {
            if (c == '.')
                ++dots;
            else if (!std::isdigit(c))
                numeric = false;
        }
        if (numeric && dots <= 1)
            v += "%";
    }

    return v;
}

static std::string normalize_sparse_infill_pattern(std::string v)
{
    boost::algorithm::to_lower(v);
    boost::algorithm::trim(v);
    // common separators / spacing
    boost::algorithm::replace_all(v, " ", "");
    boost::algorithm::replace_all(v, "_", "");
    boost::algorithm::replace_all(v, "-", "");

    // Aliases produced by LLMs / users.
    if (v == "hatching" || v == "hatch" || v == "crosshatching")
        return "crosshatch";
    if (v == "alignedrectilinear" || v == "alignedrectilineargrid" || v == "alignedrectilinearpattern")
        return "alignedrectilinear";
    if (v == "rectilineargrid")
        return "rectilinear";

    // Keep original if already canonical-ish (but stripped); restore a few with dashes.
    if (v == "trihexagon")
        return "tri-hexagon";
    if (v == "3dhoneycomb")
        return "3dhoneycomb";
    if (v == "lateralhoneycomb")
        return "lateral-honeycomb";
    if (v == "laterallattice")
        return "lateral-lattice";

    return v;
}

struct SetConfigMutateResult
{
    bool                     ok{false};
    int                      attempted{0};
    int                      changed{0};
    std::string              errors;
    std::vector<std::string> applied_kvs;
};

static bool try_deserialize_config_key(DynamicPrintConfig& cfg, const std::string& key, std::string val,
                                       ConfigSubstitutionContext& ctxt)
{
    bool ok = cfg.set_deserialize_nothrow(key, val, ctxt, false);
    if (!ok && key == "sparse_infill_pattern") {
        const std::string mapped = normalize_sparse_infill_pattern(val);
        if (mapped != val)
            ok = cfg.set_deserialize_nothrow(key, mapped, ctxt, false);
    }
    if (!ok && key == "brim_type") {
        const std::string mapped = normalize_brim_type(val);
        if (mapped != val)
            ok = cfg.set_deserialize_nothrow(key, mapped, ctxt, false);
    }
    return ok;
}

static SetConfigMutateResult mutate_set_config_on_config(DynamicPrintConfig& cfg, const nlohmann::json& action,
                                                         bool skip_unchanged)
{
    SetConfigMutateResult out;
    if (!action.contains("options") || !action["options"].is_object()) {
        out.errors = "set_config: missing options object";
        return out;
    }

    const std::string preset  = action.value("preset", "print");
    nlohmann::json    options = action["options"];
    expand_brim_options(options);
    OllamaActionExecutor::normalize_set_config_options(options);

    DynamicPrintConfig        trial = cfg;
    ConfigSubstitutionContext ctxt{ForwardCompatibilitySubstitutionRule::Disable};

    auto note_error = [&out](const std::string& msg) {
        if (!out.errors.empty())
            out.errors += "; ";
        out.errors += msg;
    };

    // Resilient batch apply: a single bad key (blocked / unknown / unparsable)
    // must NOT discard the other valid changes. We skip the offending key,
    // record why, and commit everything that deserialized cleanly. This is what
    // keeps a hands-free "인필 20%, 브림 켜줘" request working even when the LLM
    // slips in one stray option.
    for (auto it = options.begin(); it != options.end(); ++it) {
        ++out.attempted;
        const std::string raw_key = it.key();
        const std::string key     = OllamaActionExecutor::normalize_config_key(raw_key);

        if (!OllamaSettingRegistry::is_allowed_key(key, preset)) {
            note_error("blocked key for preset " + preset + ": " + raw_key);
            continue;
        }
        if (!trial.has(key)) {
            note_error("unknown option: " + raw_key);
            continue;
        }

        const std::string     val  = normalize_config_value(json_value_to_config_string(it.value()), key);
        DynamicPrintConfig    next = trial;
        if (!try_deserialize_config_key(next, key, val, ctxt)) {
            note_error("failed: " + key);
            continue;
        }

        if (skip_unchanged) {
            try {
                if (cfg.opt_serialize(key) == next.opt_serialize(key))
                    continue;
            } catch (...) {
                BOOST_LOG_TRIVIAL(warning) << "Ollama executor: opt_serialize failed comparing key " << key;
            }
        }

        trial = std::move(next);
        ++out.changed;
        std::string stored = val;
        try {
            stored = trial.opt_serialize(key);
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "Ollama executor: opt_serialize failed for key " << key;
        }
        out.applied_kvs.push_back(key + "=" + stored);
    }

    cfg = std::move(trial);
    // ok when we changed something, or when there was simply nothing to change
    // (all no-ops). It's only a hard failure if every requested key errored out.
    out.ok = out.changed > 0 || out.errors.empty();
    return out;
}

OllamaActionResult apply_set_config(const nlohmann::json& action)
{
    OllamaActionResult result;
    if (!wxGetApp().plater()) {
        result.message = "Plater not available";
        return result;
    }

    const Preset::Type    ptype = preset_type_from_string(action.value("preset", "print"));
    DynamicPrintConfig*   cfg   = edited_config_for_action(action);
    if (!cfg) {
        result.message = "Preset bundle not available";
        return result;
    }

    const std::string          preset = action.value("preset", "print");
    const SetConfigMutateResult mut   = mutate_set_config_on_config(*cfg, action, /*skip_unchanged*/ true);

    if (!mut.ok) {
        result.message = mut.errors.empty() ? "No settings updated" : mut.errors;
        OllamaTelemetry::set_config_applied(preset, mut.attempted, 0, false);
        return result;
    }

    if (mut.changed == 0) {
        result.success           = true;
        result.effective_change  = false;
        result.message           = "No settings changed (already at target values)";
        OllamaTelemetry::set_config_noop(preset);
        return result;
    }

    if (Tab* tab = wxGetApp().get_tab(ptype)) {
        tab->update_dirty();
        tab->reload_config();
    }
    if (auto* plater = wxGetApp().plater()) {
        plater->on_config_change(wxGetApp().preset_bundle->full_config(false));
        plater->sidebar().update_presets(ptype);
        if (wxGetApp().mainframe)
            wxGetApp().mainframe->update_side_preset_ui();
    }

    result.success          = true;
    result.effective_change = true;
    OllamaTelemetry::set_config_applied(preset, mut.attempted, mut.changed, false);

    if (mut.changed == 1 && mut.applied_kvs.size() == 1) {
        const std::string& kv = mut.applied_kvs.front();
        if (kv.find("brim_width") != std::string::npos && kv.find("=0") == std::string::npos)
            result.message = "Brim enabled";
        else if (kv.find("brim_width=0") != std::string::npos)
            result.message = "Brim disabled";
        else if (kv.find("enable_support") != std::string::npos)
            result.message = "Supports enabled";
        else if (kv.find("sparse_infill_density") != std::string::npos)
            result.message = "Infill updated";
        else
            result.message = "Print setting updated";
    } else {
        result.message = (boost::format("Updated %1% print setting(s)") % mut.changed).str();
    }
    return result;
}

OllamaActionResult apply_transform(const nlohmann::json& action, const char* type_name)
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    ensure_prepare_view_for_object_ops();
    GLCanvas3D* canvas = view3d_canvas_for_object_ops(plater);
    if (!canvas) {
        result.message = "3D view not available";
        return result;
    }

    Selection& sel = canvas->get_selection();
    if (sel.is_empty() && !ensure_selection_for_object_ops(plater, &action)) {
        const std::vector<unsigned int> on_plate = object_indices_on_current_plate(plater);
        if (on_plate.empty())
            result.message = "No models on the current plate";
        else if (on_plate.size() > 1 && action_has_object_target(action))
            result.message = "Could not find the requested model — check object_id or object_name";
        else
            result.message = "Select at least one object on the plate";
        return result;
    }

    TransformationType tt;
    tt.set_relative();
    tt.set_instance();

    sel.setup_cache();

    if (std::string(type_name) == "translate") {
        const double x = action.value("x", 0.0);
        const double y = action.value("y", 0.0);
        const double z = action.value("z", 0.0);
        sel.translate(Vec3d(x, y, z), tt);
        plater->take_snapshot("AI Assistant: Move", UndoRedo::SnapshotType::GizmoAction);
        canvas->do_move("");
        result.success          = true;
        result.effective_change = true;
        result.message = (boost::format("Moved selection by (%1%, %2%, %3%) mm") % x % y % z).str();
    } else if (std::string(type_name) == "rotate") {
        const double x_deg = action.value("x", 0.0);
        const double y_deg = action.value("y", 0.0);
        const double z_deg = action.value("z", 0.0);
        const bool z_only  = std::abs(x_deg) < 0.01 && std::abs(y_deg) < 0.01 && std::abs(z_deg) > 0.01;

        if (z_only) {
            if (const auto current_z = selection_instance_z_degrees(sel)) {
                const double target_z = normalize_rotation_degrees(z_deg);
                if (rotation_degrees_equivalent(*current_z, target_z)) {
                    result.success          = true;
                    result.effective_change = false;
                    result.message          = "Selection is already at the requested rotation";
                    return result;
                }
                const double delta_deg = normalize_rotation_degrees(target_z - *current_z);
                const double rz        = delta_deg * kPi / 180.0;
                sel.rotate(Vec3d(0.0, 0.0, rz), tt);
            } else {
                const double rz = z_deg * kPi / 180.0;
                sel.rotate(Vec3d(0.0, 0.0, rz), tt);
            }
        } else {
            const double rx = x_deg * kPi / 180.0;
            const double ry = y_deg * kPi / 180.0;
            const double rz = z_deg * kPi / 180.0;
            sel.rotate(Vec3d(rx, ry, rz), tt);
        }
        plater->take_snapshot("AI Assistant: Rotate", UndoRedo::SnapshotType::GizmoAction);
        canvas->do_rotate("");
        result.success          = true;
        result.effective_change = true;
        if (std::abs(z_deg) > 0.01 && std::abs(x_deg) < 0.01 && std::abs(y_deg) < 0.01)
            result.message = (boost::format("Rotated selection to %1%° on the bed") % normalize_rotation_degrees(z_deg)).str();
        else
            result.message = "Rotated selection";
    } else if (std::string(type_name) == "scale") {
        const bool uniform = action.value("uniform", true);
        double fx          = action.value("factor", action.value("x", 1.0));
        double fy          = action.value("y", fx);
        double fz          = action.value("z", fx);
        if (uniform)
            fy = fz = fx;
        if (fx <= 0.0 || fy <= 0.0 || fz <= 0.0) {
            result.message = "Scale factor must be positive";
            return result;
        }
        TransformationType st;
        st.set_world();
        st.set_relative();
        st.set_joint();
        sel.scale(Vec3d(fx, fy, fz), st);
        plater->take_snapshot("AI Assistant: Scale", UndoRedo::SnapshotType::GizmoAction);
        canvas->do_scale("");
        result.success          = true;
        result.effective_change = true;
        result.message = (boost::format("Scaled selection (x%1%, x%2%, x%3%)") % fx % fy % fz).str();
    } else {
        result.message = "Unknown transform type";
    }

    return result;
}

OllamaActionResult apply_ui_select_tab(const nlohmann::json& action)
{
    OllamaActionResult result;
    MainFrame* mf = wxGetApp().mainframe;
    if (!mf) {
        result.message = "Main window not available";
        return result;
    }

    const std::string tab = action.value("tab", "");
    MainFrame::TabPosition pos = MainFrame::tp3DEditor;
    if (tab == "prepare" || tab == "editor" || tab == "3d")
        pos = MainFrame::tp3DEditor;
    else if (tab == "preview")
        pos = MainFrame::tpPreview;
    else if (tab == "monitor")
        pos = MainFrame::tpMonitor;
    else if (tab == "smart_print") {
        if (mf->page_index_for(MainFrame::tpSmartPrint) != wxNOT_FOUND)
            mf->select_tab(MainFrame::tpSmartPrint);
        else
            wxGetApp().open_smart_print();
        result.success = true;
        result.message = "Switched tab to " + tab;
        return result;
    }
    else if (tab == "home")
        pos = MainFrame::tpHome;
    else if (tab == "calibration") {
        if (mf->page_index_for(MainFrame::tpCalibration) == wxNOT_FOUND) {
            result.message = "Calibration tab not available";
            return result;
        }
        mf->select_tab(MainFrame::tpCalibration);
        result.success          = true;
        result.effective_change = true;
        result.message          = "Switched tab to calibration";
        return result;
    }
    else {
        result.message = "Unknown tab: " + tab;
        return result;
    }

    mf->select_tab(pos);
    result.success = true;
    result.message = "Switched tab to " + tab;
    return result;
}

OllamaActionResult apply_plater_event(int evt_id, const std::string& label)
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    wxPostEvent(plater, SimpleEvent(evt_id));
    result.success          = true;
    result.effective_change = true;
    result.message          = label;
    return result;
}

OllamaActionResult apply_slice(const nlohmann::json& action)
{
    const std::string scope = action.value("scope", "plate");
    if (scope == "all")
        return apply_plater_event(EVT_GLTOOLBAR_SLICE_ALL, "Started slicing (all plates)");
    return apply_plater_event(EVT_GLTOOLBAR_SLICE_PLATE, "Started slicing (current plate)");
}

OllamaActionResult apply_delete_selection()
{
    return apply_plater_event(EVT_GLTOOLBAR_DELETE, "Deleted selection");
}

OllamaActionResult apply_clone_selection()
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    if (!ensure_selection_for_object_ops(plater, nullptr)) {
        result.message = "Select at least one object to copy";
        return result;
    }
    return apply_plater_event(EVT_GLTOOLBAR_CLONE, "Cloned selection");
}

OllamaActionResult apply_arrange()
{
    return apply_plater_event(EVT_GLTOOLBAR_ARRANGE, "Arranged models on the plate");
}

OllamaActionResult apply_arrange_objects()
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    if (!plate) {
        result.message = "No active plate";
        return result;
    }
    plate->set_print_seq(PrintSequence::ByObject);
    plater->on_config_change(wxGetApp().preset_bundle->full_config(false));
    if (!plater->can_arrange()) {
        result.message = "Cannot arrange objects right now";
        return result;
    }
    return apply_plater_event(EVT_GLTOOLBAR_ARRANGE, "Arranged models by-object on the plate");
}

OllamaActionResult apply_split_object()
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    if (!plater->can_split_to_objects()) {
        result.message = "Select a splittable model on the plate";
        return result;
    }
    return apply_plater_event(EVT_GLTOOLBAR_SPLIT_OBJECTS, "Split model into separate objects");
}

OllamaActionResult apply_get_state(const nlohmann::json& action)
{
    (void) action;
    OllamaActionResult result;
    result.success          = true;
    result.effective_change = false;
    result.message          = OllamaAgentStateService::snapshot_json();
    return result;
}

OllamaActionResult apply_list_objects(const nlohmann::json& action)
{
    (void) action;
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    nlohmann::json arr = nlohmann::json::array();
    const Model&   model = plater->model();
    for (size_t i = 0; i < model.objects.size(); ++i) {
        arr.push_back(nlohmann::json::object({
            {"id", i},
            {"name", model.objects[i]->name},
        }));
    }
    result.success          = true;
    result.effective_change = false;
    result.message          = arr.dump(2);
    return result;
}

OllamaActionResult apply_select_object(const nlohmann::json& action)
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    if (!select_objects_for_action(plater, action)) {
        result.message = "Could not select object — check object_id or object_name";
        return result;
    }
    result.success          = true;
    result.effective_change = true;
    result.message          = "Selection updated";
    return result;
}

OllamaActionResult apply_add_plate()
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    if (!plater->can_add_plate()) {
        result.message = "Cannot add plate right now";
        return result;
    }
    GLCanvas3D* canvas = view3d_canvas_for_object_ops(plater);
    wxGLCanvas* wx_canvas = canvas ? canvas->get_wxglcanvas() : nullptr;
    if (!wx_canvas) {
        result.message = "3D view not available";
        return result;
    }
    wxPostEvent(wx_canvas, SimpleEvent(EVT_GLTOOLBAR_ADD_PLATE));
    result.success          = true;
    result.effective_change = true;
    result.message          = "Added plate";
    return result;
}

OllamaActionResult apply_delete_plate()
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    if (!plater->can_delete_plate()) {
        result.message = "Cannot delete plate right now";
        return result;
    }
    GLCanvas3D* canvas = view3d_canvas_for_object_ops(plater);
    wxGLCanvas* wx_canvas = canvas ? canvas->get_wxglcanvas() : nullptr;
    if (!wx_canvas) {
        result.message = "3D view not available";
        return result;
    }
    wxPostEvent(wx_canvas, SimpleEvent(EVT_GLTOOLBAR_DEL_PLATE));
    result.success          = true;
    result.effective_change = true;
    result.message          = "Deleted plate";
    return result;
}

OllamaActionResult apply_select_plate(const nlohmann::json& action)
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    if (!action.contains("plate_index") || !action["plate_index"].is_number_integer()) {
        result.message = "select_plate: missing plate_index";
        return result;
    }
    const int idx = action["plate_index"].get<int>();
    if (plater->select_plate(idx) != 0) {
        result.message = "select_plate failed";
        return result;
    }
    result.success          = true;
    result.effective_change = true;
    result.message          = "Selected plate " + std::to_string(idx);
    return result;
}

OllamaActionResult apply_open_calibration()
{
    OllamaActionResult result;
    MainFrame* mf = wxGetApp().mainframe;
    if (!mf) {
        result.message = "Main window not available";
        return result;
    }
    if (mf->page_index_for(MainFrame::tpCalibration) == wxNOT_FOUND) {
        result.message = "Calibration tab not available";
        return result;
    }
    mf->select_tab(MainFrame::tpCalibration);
    result.success          = true;
    result.effective_change = true;
    result.message          = "Opened calibration tab";
    return result;
}

OllamaActionResult apply_select_preset(const nlohmann::json& action)
{
    OllamaActionResult result;
    const std::string name = action.value("name", "");
    if (name.empty()) {
        result.message = "select_preset: missing name";
        return result;
    }
    std::string preset_kind = action.value("preset", "print");
    Preset::Type type       = Preset::TYPE_PRINT;
    if (preset_kind == "filament")
        type = Preset::TYPE_FILAMENT;
    else if (preset_kind == "printer")
        type = Preset::TYPE_PRINTER;
    Tab* tab = wxGetApp().get_tab(type);
    if (!tab) {
        result.message = "Preset tab not available";
        return result;
    }
    if (!tab->select_preset(name)) {
        result.message = "Preset not found: " + name;
        return result;
    }
    result.success          = true;
    result.effective_change = true;
    result.message          = "Selected " + preset_kind + " preset: " + name;
    return result;
}

OllamaActionResult apply_save_project(const nlohmann::json& action)
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    const bool save_as = action.value("save_as", false);
    if (plater->save_project(save_as) != 0) {
        result.message = "Save project failed or was cancelled";
        return result;
    }
    result.success          = true;
    result.effective_change = true;
    result.message          = save_as ? "Saved project (Save As)" : "Saved project";
    return result;
}

OllamaActionResult apply_add_model(const nlohmann::json& action)
{
    OllamaActionResult result;
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        result.message = "Plater not available";
        return result;
    }
    if (!action.contains("path") || !action["path"].is_string()) {
        result.message = "add_model: missing string 'path'";
        return result;
    }
    const std::string path = action["path"].get<std::string>();
    if (path.empty()) {
        result.message = "add_model: empty path";
        return result;
    }
    // Validate path early to avoid confusing "no geometry" errors.
    {
        const wxString wx_path = wxString::FromUTF8(path);
        const wxFileName fn(wx_path);
        if (!fn.IsAbsolute()) {
            result.message = "add_model: path must be absolute";
            return result;
        }
        if (!wxFileExists(wx_path)) {
            result.message = "add_model: file not found";
            return result;
        }
        std::error_code ec;
        const auto fsize = std::filesystem::file_size(std::filesystem::path(path), ec);
        if (!ec && fsize == 0) {
            result.message = "add_model: file is empty";
            return result;
        }
        std::string ext = fn.GetExt().utf8_string();
        boost::algorithm::to_lower(ext);
        // Keep this list intentionally strict to prevent trying to load random non-geometry files.
        static const std::unordered_set<std::string> kAllowed = {
            "stl", "3mf", "obj", "amf", "step", "stp",
        };
        if (ext.empty()) {
            result.message = "add_model: missing file extension";
            return result;
        }
        if (kAllowed.find(ext) == kAllowed.end()) {
            result.message = "add_model: unsupported file type (." + ext + ")";
            return result;
        }
    }
    plater->add_model(false, path);
    result.success = true;
    result.message = "Imported model: " + path;
    return result;
}

OllamaActionResult apply_menu_item(const nlohmann::json& action)
{
    OllamaActionResult result;
    MainFrame* mf = wxGetApp().mainframe;
    if (!mf) {
        result.message = "Main window not available";
        return result;
    }

    const std::string menu = action.value("menu", "");
    const std::string item = action.value("item", "");
    if (menu.empty() || item.empty()) {
        result.message = "menu_item: requires 'menu' and 'item'";
        return result;
    }

    const std::string key = menu + "|" + item;
    // Explicitly block G-code export via AI.
    if (boost::iequals(item, "Export G-code") || boost::istarts_with(item, "Export G-code")) {
        result.message = "Blocked: Export G-code is disabled for AI control";
        return result;
    }
    // Block any save/export actions via AI (user request).
    // We keep this heuristic broad because menu labels differ by locale.
    auto contains_ci = [](const std::string& hay, const char* needle) {
        return boost::ifind_first(hay, needle);
    };
    const bool is_file_menu = boost::iequals(menu, "File") || boost::iequals(menu, "파일");
    if (is_file_menu) {
        if (contains_ci(item, "save") || contains_ci(item, "export") || contains_ci(item, "저장") || contains_ci(item, "내보내")) {
            result.message = "Blocked: file saving/export is disabled for AI control";
            return result;
        }
    }
    if (contains_ci(item, "quit") || contains_ci(item, "exit")) {
        result.message = "Blocked: quitting the application is disabled for AI control";
        return result;
    }
    if (is_blocked_calibration_menu_item(menu, item)) {
        result.message = "Blocked: calibration wizards (e.g. VFA test) are disabled for AI control";
        return result;
    }
    if (!mf->open_menubar_item(wxString::FromUTF8(menu), wxString::FromUTF8(item))) {
        result.message = "Menu item not found: " + key;
        return result;
    }
    result.success = true;
    result.message = "Menu: " + key;
    return result;
}

} // namespace

std::string OllamaActionExecutor::normalize_config_key(const std::string& key)
{
    return normalize_config_key_local(key);
}

void OllamaActionExecutor::normalize_set_config_options(nlohmann::json& options)
{
    normalize_set_config_options_impl(options);
}

void OllamaActionExecutor::augment_actions_from_user_text(nlohmann::json& /*root*/, const std::string& /*user_request*/)
{
}

void OllamaActionExecutor::boost_actions_from_user_text(nlohmann::json& /*root*/, const std::string& /*user_request*/)
{
}

void OllamaActionExecutor::augment_geometry_object_targets(nlohmann::json& root, const std::string& user_request)
{
    augment_geometry_object_targets_impl(root, user_request);
}

OllamaSetConfigDryRunResult OllamaActionExecutor::dry_run_set_config(const nlohmann::json& action)
{
    OllamaSetConfigDryRunResult out;
#ifdef OLLAMA_HEADLESS_TEST
    (void) action;
    out.ok        = true;
    out.attempted = true;
    out.changed   = true;
    return out;
#endif
    const Preset::Type          ptype = preset_type_from_string(action.value("preset", "print"));
    DynamicPrintConfig*         live  = edited_config_for_action(action);
    if (!live) {
        out.errors = "Preset bundle not available";
        return out;
    }
    DynamicPrintConfig      trial = *live;
    const SetConfigMutateResult mut = mutate_set_config_on_config(trial, action, /*skip_unchanged*/ true);
    out.ok        = mut.ok;
    out.attempted = mut.attempted;
    out.changed   = mut.changed;
    out.errors    = mut.errors;
    return out;
}

void OllamaActionExecutor::apply_set_config_actions_to_config(DynamicPrintConfig& cfg, const nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    for (const auto& action : root["actions"]) {
        if (!action.is_object() || action.value("type", "") != "set_config")
            continue;
        const SetConfigMutateResult mut = mutate_set_config_on_config(cfg, action, /*skip_unchanged*/ true);
        if (!mut.ok)
            break;
    }
}

void OllamaActionExecutor::invalidate_context_cache()
{
    OllamaContextBuilder::invalidate_context_cache();
}

void OllamaActionExecutor::notify_plater_context_changed(bool clear_coach_dedup)
{
    OllamaContextBuilder::notify_plater_context_changed(clear_coach_dedup);
}

std::string OllamaActionExecutor::fit_context_json_to_limit(std::string json, size_t max_chars)
{
    return OllamaContextBuilder::fit_context_json_to_limit(std::move(json), max_chars);
}

std::string OllamaActionExecutor::build_system_prompt(bool apply_mode)
{
    const bool ko = OllamaContextBuilder::ui_prefers_korean();
    if (apply_mode) {
        std::string prompt = OllamaSystemPrompts::apply_system_prompt(ko);
        prompt += OllamaUserFlow::flow_prompt_block(ko);
        return prompt;
    }
    return OllamaSystemPrompts::question_system_prompt(ko);
}

std::string OllamaActionExecutor::build_context_json()
{
    return OllamaContextBuilder::build_context_json();
}

std::string OllamaActionExecutor::build_compact_context_json()
{
    return OllamaContextBuilder::build_compact_context_json();
}

nlohmann::json OllamaActionExecutor::extract_action_json(const std::string& assistant_text)
{
    return extract_ollama_action_json(assistant_text);
}

std::vector<OllamaActionResult> OllamaActionExecutor::execute(const nlohmann::json& root)
{
    std::vector<OllamaActionResult> results;
    if (wxGetApp().is_closing())
        return results;
    if (!root.contains("actions") || !root["actions"].is_array())
        return results;

    // Applying AI actions may switch presets or reload configs that would
    // otherwise raise "unsaved changes" / substitution modals. The user asked
    // for these to resolve automatically (discard), so suppress them for the
    // whole batch. Re-entrant: nests safely inside an orchestrator scope.
    AiAutomationScope automation_scope;

    bool needs_batch_snapshot = false;
    for (const auto& action : root["actions"]) {
        if (!action.is_object() || !action.contains("type") || !action["type"].is_string())
            continue;
        const std::string type = action["type"].get<std::string>();
        if (type != "translate" && type != "rotate" && type != "scale")
            needs_batch_snapshot = true;
    }
    if (needs_batch_snapshot) {
        if (Plater* plater = wxGetApp().plater())
            plater->take_snapshot("AI Assistant", UndoRedo::SnapshotType::Action);
    }

    bool config_effective_change = false;
    bool had_slice_action        = false;
    nlohmann::json deferred_slice = nlohmann::json::object({{"scope", "plate"}});
    std::unordered_set<std::string> plate_action_fps;
    std::unordered_set<std::string> mesh_action_fps;
    bool has_mesh_mutation = false;
    for (const auto& action : root["actions"]) {
        if (!action.is_object() || !action.contains("type"))
            continue;
        const std::string type = action["type"].get<std::string>();
        if (type == "repair_mesh" || type == "mirror_mesh" || type == "mesh_boolean" || type == "split_object"
            || type == "split_mesh")
            has_mesh_mutation = true;
    }

    for (const auto& action : root["actions"]) {
        if (!action.contains("type") || !action["type"].is_string())
            continue;

        const std::string type = action["type"].get<std::string>();
        if (type == "slice") {
            had_slice_action = true;
            deferred_slice   = action;
            continue;
        }

        if (type == "arrange" || type == "arrange_objects" || type == "clone_selection" || type == "rotate" || type == "translate"
            || type == "scale") {
            const std::string fp = type + "|" + action.dump();
            if (!plate_action_fps.insert(fp).second) {
                OllamaActionResult skipped;
                skipped.success          = true;
                skipped.effective_change = false;
                skipped.message          = "Skipped duplicate " + type;
                results.push_back(std::move(skipped));
                continue;
            }
        }

        if (type == "repair_mesh" || type == "mirror_mesh") {
            if (!mesh_action_fps.insert(type).second) {
                OllamaActionResult skipped;
                skipped.success          = true;
                skipped.effective_change = false;
                skipped.message          = "Skipped duplicate " + type;
                results.push_back(std::move(skipped));
                continue;
            }
        } else if (type == "mesh_boolean") {
            const std::string fp = type + "|" + action.value("operation", "");
            if (!mesh_action_fps.insert(fp).second) {
                OllamaActionResult skipped;
                skipped.success          = true;
                skipped.effective_change = false;
                skipped.message          = "Skipped duplicate " + type;
                results.push_back(std::move(skipped));
                continue;
            }
        }

        OllamaActionResult result;
        if (type == "get_state")
            result = apply_get_state(action);
        else if (type == "list_objects")
            result = apply_list_objects(action);
        else if (type == "select_object")
            result = apply_select_object(action);
        else if (type == "set_config")
            result = apply_set_config(action);
        else if (type == "ui_select_tab")
            result = apply_ui_select_tab(action);
        else if (type == "open_calibration")
            result = apply_open_calibration();
        else if (type == "delete_selection")
            result = apply_delete_selection();
        else if (type == "clone_selection")
            result = apply_clone_selection();
        else if (type == "arrange")
            result = apply_arrange();
        else if (type == "arrange_objects")
            result = apply_arrange_objects();
        else if (type == "split_object" || type == "split_mesh")
            result = apply_split_object();
        else if (type == "save_project")
            result = apply_save_project(action);
        else if (type == "add_plate")
            result = apply_add_plate();
        else if (type == "delete_plate")
            result = apply_delete_plate();
        else if (type == "select_plate")
            result = apply_select_plate(action);
        else if (type == "select_preset")
            result = apply_select_preset(action);
        else if (type == "add_model")
            result = apply_add_model(action);
        else if (type == "menu_item")
            result = apply_menu_item(action);
        else if (type == "translate" || type == "rotate" || type == "scale")
            result = apply_transform(action, type.c_str());
        else if (type == "repair_mesh")
            result = OllamaMeshOps::apply_repair_mesh(action);
        else if (type == "mirror_mesh")
            result = OllamaMeshOps::apply_mirror_mesh(action);
        else if (type == "mesh_boolean")
            result = OllamaMeshOps::apply_mesh_boolean(action);
        else if (type == "open_smart_print" || type == "run_smart_print" || type == "open_setup" || type == "send_print"
                 || type == "export_gcode" || type == "rollback_apply")
            result = OllamaUserFlow::apply_flow_action(action, wxGetApp().plater());
        else if (type == "makerworld_search" || type == "makerworld_find_and_print" || type == "import_makerworld")
            result = OllamaMakerWorldActions::apply(action);
        else
            result = OllamaActionResult{false, false, "Unknown action: " + type};

        if (type == "set_config" && result.effective_change)
            config_effective_change = true;
        if (!result.message.empty() && result.message.find("Skipped duplicate") == std::string::npos)
            OllamaTelemetry::action_executed(type, result.success, result.effective_change);
        results.push_back(std::move(result));
    }

    if (config_effective_change && !has_mesh_mutation) {
        invalidate_context_cache();
        results.push_back(apply_slice(nlohmann::json::object({{"scope", "plate"}})));
        OllamaIntentContext::mark_pending_slice_feedback();
    } else if (had_slice_action && !has_mesh_mutation) {
        results.push_back(apply_slice(deferred_slice));
    }

    return results;
}

void OllamaActionExecutor::coalesce_set_config_action_options(nlohmann::json& action)
{
    if (!action.is_object() || action.value("type", "") != "set_config")
        return;
    auto merge_alias = [&](const char* alias) {
        if (!action.contains(alias) || !action[alias].is_object() || action[alias].empty())
            return;
        if (!action.contains("options") || !action["options"].is_object())
            action["options"] = nlohmann::json::object();
        for (auto it = action[alias].begin(); it != action[alias].end(); ++it) {
            if (!action["options"].contains(it.key()))
                action["options"][it.key()] = it.value();
        }
        action.erase(alias);
    };
    merge_alias("values");
    merge_alias("settings");
    merge_alias("params");
    if (action.contains("options") && action["options"].is_object() && action["options"].empty())
        action.erase("options");
}

nlohmann::json OllamaActionExecutor::normalized_set_config_options_from_action(const nlohmann::json& action)
{
    nlohmann::json work = action;
    coalesce_set_config_action_options(work);
    if (!work.contains("options") || !work["options"].is_object())
        return nlohmann::json::object();
    nlohmann::json options = work["options"];
    expand_brim_options(options);
    normalize_set_config_options_impl(options);
    return options;
}

static bool serialized_values_equivalent(const std::string& live, const std::string& expected)
{
    if (live == expected)
        return true;
    if (live == "1" && (expected == "true" || expected == "1"))
        return true;
    if (live == "0" && (expected == "false" || expected == "0"))
        return true;
    return false;
}

std::vector<std::string> OllamaActionExecutor::verify_set_config_applied(const nlohmann::json& action,
                                                                          const DynamicPrintConfig* live_cfg)
{
    std::vector<std::string> mismatches;
    if (!action.is_object() || action.value("type", "") != "set_config")
        return mismatches;

    DynamicPrintConfig live;
    if (live_cfg)
        live = *live_cfg;
    else if (wxGetApp().preset_bundle)
        live = wxGetApp().preset_bundle->full_config(false);
    else
        return mismatches;

    const nlohmann::json options = normalized_set_config_options_from_action(action);
    if (!options.is_object() || options.empty())
        return mismatches;

    const std::string preset = action.value("preset", "print");
    ConfigSubstitutionContext ctxt{ForwardCompatibilitySubstitutionRule::Disable};

    for (auto it = options.begin(); it != options.end(); ++it) {
        const std::string raw_key = it.key();
        const std::string key     = normalize_config_key_local(raw_key);
        if (!live.has(key))
            continue;
        if (!OllamaSettingRegistry::is_allowed_key(key, preset))
            continue;

        const std::string val = normalize_config_value(json_value_to_config_string(it.value()), key);
        DynamicPrintConfig expected_cfg = live;
        if (!try_deserialize_config_key(expected_cfg, key, val, ctxt)) {
            mismatches.push_back(key + " (could not deserialize expected value: " + val + ")");
            continue;
        }

        std::string expected_live;
        std::string actual_live;
        try {
            expected_live = expected_cfg.opt_serialize(key);
            actual_live   = live.opt_serialize(key);
        } catch (...) {
            continue;
        }

        if (!serialized_values_equivalent(actual_live, expected_live))
            mismatches.push_back(key + " (expected " + expected_live + ", got " + actual_live + ")");
    }

    return mismatches;
}

nlohmann::json OllamaActionExecutor::config_digest_for_set_config_actions(const nlohmann::json& root,
                                                                          const std::string& user_goal_hint)
{
    nlohmann::json out = nlohmann::json::object();
    if (!wxGetApp().preset_bundle)
        return out;

    std::unordered_set<std::string> keys;
    if (root.contains("actions") && root["actions"].is_array()) {
        for (const auto& action : root["actions"]) {
            if (!action.is_object() || action.value("type", "") != "set_config")
                continue;
            const nlohmann::json options = normalized_set_config_options_from_action(action);
            if (!options.is_object())
                continue;
            for (auto it = options.begin(); it != options.end(); ++it)
                keys.insert(normalize_config_key_local(it.key()));
        }
    }

    if (keys.empty()) {
        const std::vector<std::string> hinted =
            OllamaSettingSearch::candidate_keys_for_request(user_goal_hint, 2, 12);
        for (const std::string& key : hinted)
            keys.insert(normalize_config_key_local(key));
    }

    if (keys.empty()) {
        static const char* kDefaultKeys[] = {
            "layer_height", "sparse_infill_density", "enable_support", "brim_width",
            "retraction_length", "nozzle_temperature", "bed_temperature", "outer_wall_speed",
        };
        for (const char* k : kDefaultKeys)
            keys.insert(k);
    }

    const DynamicPrintConfig cfg = wxGetApp().preset_bundle->full_config(false);
    for (const std::string& key : keys) {
        if (!cfg.has(key))
            continue;
        const ConfigOption* opt = cfg.option(key);
        if (opt)
            out[key] = opt->serialize();
    }
    return out;
}

}} // namespace
