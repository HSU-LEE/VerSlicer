#include "OllamaActionExecutor.hpp"
#include "OllamaActionJsonExtract.hpp"
#include "OllamaActionValidator.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaSettingRegistry.hpp"
#include "OllamaTelemetry.hpp"

#include "../AICoach/AICoachApplyDedup.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../BambuSmartPrint/BambuSmartPrintService.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Event.hpp"
#include "slic3r/GUI/GLToolbar.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <unordered_set>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/menu.h>

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

/** Select plate objects when transforms/copy run with nothing selected (e.g. right after model load). */
bool ensure_selection_for_object_ops(Plater* plater)
{
    if (!plater || plater->model().objects.empty())
        return false;
    GLCanvas3D* canvas = plater->canvas3D();
    if (!canvas)
        return false;
    if (!canvas->get_selection().is_empty())
        return true;
    plater->select_all();
    return !canvas->get_selection().is_empty();
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
                    }
            }
            if (!has_width || w <= 0.0)
                options["brim_width"] = 5.0;
            if (!options.contains("brim_type"))
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

static bool is_calibration_menu_name(const std::string& name)
{
    return boost::icontains(name, "calib") || name.find("캘리브") != std::string::npos ||
           name.find("보정") != std::string::npos;
}

static bool is_blocked_calibration_menu_item(const std::string& menu, const std::string& item)
{
    if (is_calibration_menu_name(menu))
        return true;
    return boost::icontains(item, "VFA") || item.find("캘리브") != std::string::npos ||
           boost::icontains(item, "calibration");
}

static nlohmann::json build_menu_context_json()
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
        if (is_calibration_menu_name(menu_name))
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

    // For density, a bare number is usually a percent.
    if (key.find("density") != std::string::npos) {
        // "20" -> "20%"
        bool all_digits = !v.empty() && std::all_of(v.begin(), v.end(), [](unsigned char c) { return std::isdigit(c); });
        if (all_digits && v.find('%') == std::string::npos)
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

    for (auto it = options.begin(); it != options.end(); ++it) {
        ++out.attempted;
        const std::string raw_key = it.key();
        const std::string key     = OllamaActionExecutor::normalize_config_key(raw_key);

        if (!OllamaSettingRegistry::is_allowed_key(key, preset)) {
            if (!out.errors.empty())
                out.errors += "; ";
            out.errors += "blocked key for preset " + preset + ": " + raw_key;
            return out;
        }
        if (!trial.has(key)) {
            if (!out.errors.empty())
                out.errors += "; ";
            out.errors += "unknown option: " + raw_key;
            return out;
        }

        const std::string     val  = normalize_config_value(json_value_to_config_string(it.value()), key);
        DynamicPrintConfig    next = trial;
        if (!try_deserialize_config_key(next, key, val, ctxt)) {
            if (!out.errors.empty())
                out.errors += "; ";
            out.errors += "failed: " + key;
            return out;
        }

        if (skip_unchanged) {
            try {
                if (cfg.opt_serialize(key) == next.opt_serialize(key))
                    continue;
            } catch (...) {
            }
        }

        trial = std::move(next);
        ++out.changed;
        std::string stored = val;
        try {
            stored = trial.opt_serialize(key);
        } catch (...) {
        }
        out.applied_kvs.push_back(key + "=" + stored);
    }

    cfg    = std::move(trial);
    out.ok = true;
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
    GLCanvas3D* canvas = plater->canvas3D();
    if (!canvas) {
        result.message = "3D view not available";
        return result;
    }

    Selection& sel = canvas->get_selection();
    if (sel.is_empty() && !ensure_selection_for_object_ops(plater)) {
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
    else if (tab == "smart_print")
        pos = MainFrame::tpSmartPrint;
    else if (tab == "home")
        pos = MainFrame::tpHome;
    else {
        result.message = "Unknown tab: " + tab;
        return result;
    }

    mf->request_select_tab(pos);
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
    if (!ensure_selection_for_object_ops(plater)) {
        result.message = "Select at least one object to copy";
        return result;
    }
    return apply_plater_event(EVT_GLTOOLBAR_CLONE, "Cloned selection");
}

OllamaActionResult apply_arrange()
{
    return apply_plater_event(EVT_GLTOOLBAR_ARRANGE, "Arranged models on the plate");
}

OllamaActionResult apply_save_project(const nlohmann::json& action)
{
    OllamaActionResult result;
    (void)action;
    result.success = false;
    result.message = "Blocked: file saving is disabled for AI control";
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

static std::optional<double> parse_percent_serial(const std::string& serialized)
{
    std::string s = serialized;
    boost::algorithm::erase_all(s, "%");
    boost::algorithm::trim(s);
    if (s.empty())
        return std::nullopt;
    try {
        return std::stod(s);
    } catch (...) {
        return std::nullopt;
    }
}

static bool user_wants_more(const std::string& user)
{
    return user.find("올려") != std::string::npos || user.find("높여") != std::string::npos ||
           user.find("늘려") != std::string::npos || user.find("더 ") != std::string::npos ||
           user.find("increase") != std::string::npos || user.find("more") != std::string::npos ||
           user.find("higher") != std::string::npos;
}

static bool user_wants_less(const std::string& user)
{
    return user.find("낮춰") != std::string::npos || user.find("줄여") != std::string::npos ||
           user.find("감소") != std::string::npos || user.find("decrease") != std::string::npos ||
           user.find("less") != std::string::npos || user.find("lower") != std::string::npos;
}

static bool user_mentions_infill(const std::string& user)
{
    return user.find("채움") != std::string::npos || user.find("infill") != std::string::npos ||
           user.find("density") != std::string::npos || user.find("내부") != std::string::npos;
}

static bool user_mentions_layer_height(const std::string& user)
{
    return user.find("층") != std::string::npos || user.find("layer") != std::string::npos;
}

static void coerce_option_json(nlohmann::json& val, const std::string& key)
{
    const std::string normalized = normalize_config_value(json_value_to_config_string(val), key);
    if (key == "enable_support" || key == "enable_brim" || key == "support_on_build_plate_only") {
        val = (normalized == "1" || normalized == "true");
        return;
    }
    if (key == "sparse_infill_density") {
        val = normalized;
        return;
    }
    if (key == "wall_loops" || key == "top_shell_layers" || key == "bottom_shell_layers" ||
        key == "raft_layers") {
        try {
            val = std::stoi(normalized);
        } catch (...) {
            val = normalized;
        }
        return;
    }
    try {
        val = std::stod(normalized);
    } catch (...) {
        val = normalized;
    }
}

static void augment_actions_from_user_text_impl(nlohmann::json& root, const std::string& user)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;

    DynamicPrintConfig* cfg = edited_config(Preset::TYPE_PRINT);
    const bool more_infill  = (user_wants_more(user) || user.find("두껍") != std::string::npos) &&
                             user_mentions_infill(user) && !user_wants_less(user);
    const bool less_infill  = user_wants_less(user) && user_mentions_infill(user);

    for (auto& action : root["actions"]) {
        if (!action.is_object() || action.value("type", "") != "set_config")
            continue;
        if (!action.contains("options") || !action["options"].is_object())
            action["options"] = nlohmann::json::object();

        nlohmann::json& opts = action["options"];
        normalize_set_config_options_impl(opts);

        if (cfg) {
            if (more_infill && !opts.contains("sparse_infill_density") &&
                cfg->has("sparse_infill_density")) {
                double cur = parse_percent_serial(cfg->opt_serialize("sparse_infill_density")).value_or(15.0);
                cur        = std::min(100.0, cur + 5.0);
                opts["sparse_infill_density"] =
                    std::to_string(static_cast<int>(std::lround(cur))) + "%";
            } else if (less_infill && !opts.contains("sparse_infill_density") &&
                       cfg->has("sparse_infill_density")) {
                double cur = parse_percent_serial(cfg->opt_serialize("sparse_infill_density")).value_or(15.0);
                cur        = std::max(0.0, cur - 5.0);
                opts["sparse_infill_density"] =
                    std::to_string(static_cast<int>(std::lround(cur))) + "%";
            }

            if ((user.find("두껍") != std::string::npos || user.find("thicker") != std::string::npos) &&
                user_mentions_layer_height(user) && !opts.contains("layer_height") &&
                cfg->has("layer_height")) {
                double cur = 0.2;
                try {
                    cur = std::stod(cfg->opt_serialize("layer_height"));
                } catch (...) {
                }
                opts["layer_height"] = std::min(0.6, cur + 0.04);
            }
            if ((user.find("얇") != std::string::npos || user.find("thinner") != std::string::npos) &&
                user_mentions_layer_height(user) && !opts.contains("layer_height") &&
                cfg->has("layer_height")) {
                double cur = 0.2;
                try {
                    cur = std::stod(cfg->opt_serialize("layer_height"));
                } catch (...) {
                }
                opts["layer_height"] = std::max(0.04, cur - 0.04);
            }
        }

        OllamaIntentContext::refine_set_config_options(opts, user);

        for (auto it = opts.begin(); it != opts.end(); ++it)
            coerce_option_json(it.value(), it.key());
    }
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

void OllamaActionExecutor::augment_actions_from_user_text(nlohmann::json& root, const std::string& user_request)
{
    augment_actions_from_user_text_impl(root, user_request);
}

OllamaSetConfigDryRunResult OllamaActionExecutor::dry_run_set_config(const nlohmann::json& action)
{
    OllamaSetConfigDryRunResult out;
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

static bool ui_prefers_korean()
{
    const wxString code = GUI::wxGetApp().current_language_code();
    wxString lang = code.BeforeFirst('_').BeforeFirst('-').Lower();
    if (lang == wxString("ko"))
        return true;
    lang = GUI::wxGetApp().current_language_code_safe().BeforeFirst('_').Lower();
    return lang == wxString("ko");
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

void OllamaActionExecutor::invalidate_context_cache()
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

void OllamaActionExecutor::notify_plater_context_changed(bool clear_coach_dedup)
{
    invalidate_context_cache();
    OllamaIntentContext::refresh_cached_intent_signals();
    if (clear_coach_dedup)
        AICoachApplyDedup::instance().clear();
}

static std::string selection_bbox_signature(Plater* plater)
{
    if (!plater || !plater->canvas3D())
        return "none";
    const Selection& sel = plater->canvas3D()->get_selection();
    if (sel.is_empty())
        return "empty";
    const BoundingBoxf3 bb = sel.get_bounding_box();
    std::ostringstream  sig;
    sig << bb.min.x() << ',' << bb.min.y() << ',' << bb.min.z() << '|' << bb.size().x() << ',' << bb.size().y()
        << ',' << bb.size().z();
    return sig.str();
}

static std::string build_context_signature()
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
        if (plater->canvas3D())
            sig << plater->canvas3D()->get_selection().volumes_count() << '|';
        sig << selection_bbox_signature(plater) << '|';
        const auto& slice_a = BambuSmartPrintService::instance().last_slice_analysis();
        if (slice_a.valid) {
            sig << slice_a.overhang_area_ratio << ',' << slice_a.unsupported_islands_count << '|';
        }
        const auto& readiness = BambuSmartPrintService::instance().last_readiness_report();
        if (readiness.score > 0.f)
            sig << readiness.score << '|';
    }
    return sig.str();
}

static void append_printer_capabilities(nlohmann::json& ctx, const DynamicPrintConfig* printer_cfg)
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

static void append_slice_and_readiness(nlohmann::json& ctx, Plater* plater)
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

static nlohmann::json build_context_object(bool compact)
{
    nlohmann::json ctx;
    auto*          bundle  = wxGetApp().preset_bundle;
    Plater*        plater  = wxGetApp().plater();
    const bool     ko      = ui_prefers_korean();

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

    if (plater && plater->canvas3D()) {
        const Selection& sel = plater->canvas3D()->get_selection();
        ctx["selection_count"] = sel.volumes_count();
        ctx["has_selection"]   = !sel.is_empty();
        if (!sel.is_empty()) {
            const BoundingBoxf3 bb = sel.get_bounding_box();
            ctx["selection_size_mm"] = {
                {"x", bb.size().x()},
                {"y", bb.size().y()},
                {"z", bb.size().z()},
            };
        }
    }

    if (!compact) {
        if (bundle)
            ctx["setting_catalog"] = OllamaSettingRegistry::build_catalog(&bundle->prints.get_edited_preset().config, ko);
        else
            ctx["setting_catalog"] = OllamaSettingRegistry::build_catalog(nullptr, ko);
        ctx["audience"]            = "beginner";
        ctx["setting_rules"]       = ko
            ? "current 값 형식을 그대로 따르세요. 브림·서포트·채움은 setting_catalog 항목만 수정하세요."
            : "Match the format of each key's current value. Only change keys in setting_catalog.";
        nlohmann::json hints = nlohmann::json::object();
        if (ko) {
            hints["bed_adhesion"] = "베드에 잘 안 붙을 때: 가장자리 접착(브림)을 넓히거나 베드 온도를 조금 올립니다.";
            hints["overhang"]     = "공중으로 나오는 부분: 받침(서포트)을 켜면 성공률이 올라갑니다.";
            hints["strength"]     = "쉽게 부서질 때: 채움을 늘리거나 벽을 두껍게 합니다.";
            hints["warp"]         = "모서리가 들릴 때: 베드 온도·브림·인클로저를 점검합니다.";
            hints["stringing"]    = "실이 늘어질 때: 리트랙션·온도를 조정합니다.";
        } else {
            hints["bed_adhesion"] = "Poor bed stick: widen edge adhesion (brim) or raise bed temperature slightly.";
            hints["overhang"]     = "Floating parts: enable supports to improve success rate.";
            hints["strength"]     = "Brittle parts: increase infill or wall thickness.";
            hints["warp"]         = "Corner lift: check bed temp, brim, and enclosure.";
            hints["stringing"]    = "Stringing: tune retraction and temperature.";
        }
        ctx["plain_language_hints"] = hints;
        const nlohmann::json menu_ctx = build_menu_context_json();
        if (!menu_ctx.empty())
            ctx["menu_catalog"] = menu_ctx;
    } else {
        ctx["plain_language_hints"] = ko
            ? nlohmann::json{{"bed_adhesion", "베드 접착"}, {"overhang", "서포트"}, {"strength", "강도"}}
            : nlohmann::json{{"bed_adhesion", "bed stick"}, {"overhang", "supports"}, {"strength", "strength"}};
    }

    append_slice_and_readiness(ctx, plater);

    OllamaIntentContext::consume_slice_feedback_if_ready();
    ctx["intent_signals"] = OllamaIntentContext::build_intent_signals_json();
    OllamaIntentContext::refresh_cached_intent_signals();

    return ctx;
}

std::string OllamaActionExecutor::fit_context_json_to_limit(std::string json, size_t max_chars)
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

        if (json.size() > max_chars && ctx.contains("menu_catalog"))
            ctx.erase("menu_catalog");
        if (json.size() > max_chars && ctx.contains("plain_language_hints"))
            ctx.erase("plain_language_hints");
        json = ctx.dump(2);
        if (json.size() > max_chars) {
            const std::string compact = build_compact_context_json();
            if (compact.size() <= max_chars)
                return compact;
            json = compact;
        }
    } catch (...) {
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

std::string OllamaActionExecutor::build_system_prompt(bool apply_mode)
{
    static const char* kQuestionModeEn = R"OLLAMA(

=== QUESTION MODE (active) ===
The user is a beginner asking for help. Do NOT change the slicer.
- Always return "actions": [].
- "message": explain in plain everyday language (no unexplained jargon).
- Use short sentences. Say what the issue might be and what they could try in Apply mode, without technical JSON terms.)OLLAMA";

    static const char* kQuestionModeKo = R"OLLAMA(

=== 질문 모드 (활성) ===
초보 사용자가 도움을 요청합니다. 슬라이서 설정은 바꾸지 마세요.
- 항상 "actions": [].
- "message": 쉬운 말로만 설명 (전문 용어는 꼭 필요할 때만, 괄호로 풀어서).
- 짧은 문장. 원인 추정과 Apply 모드에서 시도해 볼 수 있는 것을 안내하세요.)OLLAMA";

    static const char* kPromptEn = R"OLLAMA(You are Verslicer AI — a patient helper inside a 3D printing slicer.

## Who you talk to
The user is often a complete beginner. They may not know words like "brim", "infill", or "support".
- Understand casual, vague, or misspelled requests (Korean or English).
- When they describe a problem, APPLY a sensible fix with set_config — do not only give theory.
- In "message", never use raw JSON keys. Use everyday words (see setting_catalog in context).

## Reading setting_catalog (critical)
Context includes setting_catalog[]: each entry has key, current, value_type, unit, min, max, format, aliases.
Also read intent_signals when present: support_recommended, still_needs_support, lay_flat_recommended, recommended_brim_width_mm, selection_footprint_mm.
Use recommended_brim_width_mm for brim_width when the user wants adhesion/brim but gives no mm value.
If still_needs_support is true after slicing, prefer enable_support unless the user clearly refuses supports.
- ALWAYS read "current" before changing a value; keep the same format (e.g. sparse_infill_density as "20%" not 20).
- bool: use true/false in JSON (enable_support, enable_brim).
- percent: string with % suffix matching current style.
- mm / count: JSON number without unit suffix.
- brim_width: 0 means off; typical on = 5 with brim_type "outer_only".
- Only use keys listed in setting_catalog or allowed_config_keys.
- Relative phrases: "more infill" / "채움 올려" → increase current sparse_infill_density by ~5% unless user gave an explicit %.

## Output (strict)
Exactly ONE JSON object. No markdown, no text outside JSON.

{
  "message": "2–4 short, friendly sentences",
  "actions": [ ... ]
}

"message" style for beginners:
1) Repeat what you understood in their words ("You said the print breaks easily…").
2) Say what you will change in plain language ("I'll turn on a brim — extra plastic around the bottom so it sticks better.").
3) Optional: one simple next step they can try if it still fails.
Same language as the user (Korean or English).

## Technical actions (hidden from user; use correctly)
set_config: { "type":"set_config", "preset":"print", "options":{ KEY: value } }
Optional: "filament_index": 0 for preset "filament" (multi-material slot, default 0).
- Keys ONLY from allowed_config_keys / print_options in context.
- Numbers as numbers; percents as "20%".
- One set_config with all related keys.

Other types: translate, rotate, scale, clone_selection, arrange, ui_select_tab, slice (only if slicing alone), delete_selection (only if user asked to delete), add_model (path required).

MakerWorld (search/import runs in app — never invent download URLs):
- makerworld_search: { "type":"makerworld_search", "query":"articulated dragon mini" }
- import_makerworld: { "type":"import_makerworld", "design_id":"12345" } OR { "type":"import_makerworld", "url":"https://makerworld.com/..." }
- Use makerworld_search when user wants to find a model. Use import_makerworld only when user picked a specific id/url.
- Search query: 2–6 concrete nouns/adjectives only (object type, style, size). No filler (find, search, model, please). Prefer English keywords when known (dragon, keycap, vase).
- Do NOT use add_model for MakerWorld links.

Brim on: brim_width 5, brim_type "outer_only" (or enable_brim true). Brim off: brim_width 0.
Supports: enable_support true.
After set_config, the app re-slices automatically — do NOT add a separate slice action.

## Everyday words → what to do (Apply mode)
| User says (examples) | Do |
| won't stick, lifts, warping, corners up / 안 붙, 들뜸, 베드 | brim |
| breaks, fragile, snaps, weak / 부서, 부러, 깨, 약, 파손, 쉽게 | brim_width 5 + outer_only; optionally wall_loops or sparse_infill_density — NOT pressure advance, NOT input shaper |
| floating, mid-air, sagging, overhang / 공중, 매달, 떨어, 오버행 | enable_support |
| hollow, soft inside, stronger / 속 비, 단단, 튼튼 | higher sparse_infill_density (e.g. "22%") or wall_loops |
| thicker/thinner layers / 두껍, 얇 | layer_height |
| flip, lay flat / 뒤집, 눕혀 | rotate |
| explicit % or "infill" / 채움 N% | sparse_infill_density |
| "what is…?", "how do I…?" only | actions [] |

If the request is vague ("fix it", "help", "고쳐줘") but mentions a symptom above, still apply the best matching fix.

## Safety
- No delete_selection unless user clearly asked to delete/remove.
- No add_model without a path. No File save/export/quit unless asked.
- No invented config keys. Never delete models to "fix" prints.

## Forbidden mistakes
- NEVER tell the user to press shortcuts or manually set brim_width in "message" — use "actions".
- NEVER open Calibration / VFA / temperature tower / flow calibration menus.
- "breaks easily" / "부서져" = print quality → brim or infill; NEVER "arrange".
- NEVER invent keys (e.g. input_shelling, pressure_advance). Use only setting_catalog keys.
- Do NOT suggest pressure advance / input shaper in "message" for fragile prints — use brim or infill actions instead.
- User asks to rotate N degrees → MUST include a "rotate" action; do not only explain.
- "message" same language throughout (no mixed foreign words).
- If actions run, "message" only confirms what you did in plain language.

## Examples (JSON only)

User: "it won't stick to the bed"
{"message":"Got it — the first layer isn't sticking well. I'll turn on a brim (extra outline around the bottom) so the print holds to the bed better.","actions":[{"type":"set_config","preset":"print","options":{"brim_width":5,"brim_type":"outer_only"}}]}

User: "출력물이 너무 쉽게 파손돼"
{"message":"말씀하신 대로 출력물이 잘 부서지는 상황이네요. 바닥이 더 잘 붙도록 브림(테두리에 플라스틱을 조금 더 깔기)을 켜 두겠습니다.","actions":[{"type":"set_config","preset":"print","options":{"brim_width":5,"brim_type":"outer_only"}}]}

User: "잘 부서져"
{"message":"쉽게 부서진다고 하셨죠. 바닥 접착을 돕기 위해 브림을 켭니다.","actions":[{"type":"set_config","preset":"print","options":{"brim_width":5,"brim_type":"outer_only"}}]}

User: "공중에 떠서 나와요"
{"message":"중간에 공중으로 나오는 부분이 있군요. 서포트(받침대)를 켜서 그 부분을 받쳐 주겠습니다.","actions":[{"type":"set_config","preset":"print","options":{"enable_support":true}}]}

User: "채움 20%로 해줘"
{"message":"안쪽을 20%로 채우도록 설정할게요. 숫자가 클수록 더 단단해집니다.","actions":[{"type":"set_config","preset":"print","options":{"sparse_infill_density":"20%"}}]}

User: "브림이 뭐예요?"
{"message":"브림은 맨 아래 테두리에 플라스틱을 조금 더 깔아서 베드에 잘 붙게 해 주는 기능이에요. 작은 물건이나 모서리가 들뜰 때 켜면 좋아요.","actions":[]})OLLAMA";

    static const char* kPromptKo = R"OLLAMA(당신은 Verslicer AI입니다. 3D 프린터 슬라이서 안에서 초보 사용자를 돕습니다.

## 대상
사용자는 슬라이서를 처음 쓰는 경우가 많습니다. "브림", "채움", "서포트" 같은 말을 모를 수 있습니다.
- 구어체·애매한 표현·오타도 의도로 이해하세요.
- 문제를 말하면 설명만 하지 말고 set_config로 합리적인 조치를 적용하세요.
- "message"에는 JSON 키 이름을 쓰지 말고, setting_catalog 설명처럼 쉬운 말을 쓰세요.

## setting_catalog 읽기 (중요)
context의 setting_catalog[]: key, current, value_type, unit, min, max, format, aliases.
intent_signals도 확인: support_recommended, still_needs_support, lay_flat_recommended, recommended_brim_width_mm, selection_footprint_mm.
브림 mm 값이 없으면 recommended_brim_width_mm을 brim_width에 사용.
still_needs_support가 true면 사용자가 거부하지 않는 한 enable_support 우선.
- 값을 바꾸기 전에 반드시 current 확인. 형식 유지 (sparse_infill_density는 "20%" 형태).
- bool: true/false. percent: "%" 포함 문자열. mm/개수: 숫자만.
- brim_width 0=끔, 켤 때 보통 5 + brim_type outer_only.
- setting_catalog에 있는 키만 사용.
- "채움 올려" 등: current에서 약 5%p 올리기 (명시적 %가 없을 때).

## 출력 (필수)
JSON 객체 하나만. 마크다운·JSON 밖 텍스트 금지.

{
  "message": "친절한 짧은 문장 2~4개",
  "actions": [ ... ]
}

"message" 작성법:
1) 사용자 말을 다시 짧게 ("쉽게 부서진다고 하셨죠.")
2) 무엇을 바꿀지 쉬운 말로 ("바닥이 잘 붙도록 브림을 켭니다.")
3) 필요하면 한 가지 추가 팁

## 기술 action (사용자에게는 숨김)
set_config: preset print, options는 allowed_config_keys / print_options만.
선택: preset "filament"일 때 "filament_index": 0 (멀티 재료 슬롯, 기본 0).
퍼센트는 "20%". set_config 후 자동 재슬라이스 — slice action 추가 금지.

## 일상 표현 → 할 일 (Apply)
| 이런 말 | 조치 |
| 안 붙, 들뜸, 베드, warp | brim |
| 부서, 부러, 깨, 약, 파손, 쉽게 | brim (+ 필요 시 채움/벽) |
| 공중, 매달, 떨어, 오버행 | enable_support |
| 속 비, 단단, 튼튼 | sparse_infill_density 상향(예 22%) 또는 wall_loops |
| 두껍/얇 | layer_height |
| 뒤집, 눕혀 | rotate |
| 채움 N% | sparse_infill_density |
| 뭐예요?, 방법만 | actions [] |

"고쳐줘"만 있어도 증상이 함께 있으면 위 표에 맞게 적용하세요.

## 안전
- 삭제 요청 없으면 delete_selection 금지. 경로 없으면 add_model 금지.
- 출력 문제로 모델 삭제 금지.

## MakerWorld (앱이 검색·다운로드 — URL을 지어내지 마세요)
- makerworld_search: { "type":"makerworld_search", "query":"articulated dragon" }
- import_makerworld: design_id 또는 makerworld.com url
- 모델을 찾아달라 → makerworld_search. 특정 id/링크 가져오기 → import_makerworld.
- query는 핵심 명사·형용사만 (찾아줘/모델/검색 등 제거). 가능하면 영어 키워드 (dragon, keycap).

## 흔한 실수 (하지 말 것)
- "message"에서 단축키·수동 설정 안내 금지 — 조치는 "actions"에 넣기.
- Calibration / VFA / 캘리브레이션 메뉴는 열지 마세요.
- "부서져", "잘 안됨" = 출력 품질 → 브림/채움; arrange(판 위 재배치)는 금지.
- input_shelling, pressure_advance 등 없는 키 금지. setting_catalog만 사용.
- 잘 부서지는 증상에 프레셔 어드밴스·입력 셰이핑 설명 대신 brim set_config.
- "N도 돌려" → 반드시 rotate action 포함.
- message는 한 언어만 (외국어 섞지 않기).
- actions가 있으면 message는 적용 확인만.

## 예시 (JSON만)

사용자: "베드에 잘 안 붙어요"
{"message":"첫 층이 잘 안 붙는다고 하셨네요. 바닥 테두리에 플라스틱을 조금 더 깔아 주는 브림을 켜 두겠습니다.","actions":[{"type":"set_config","preset":"print","options":{"brim_width":5,"brim_type":"outer_only"}}]}

사용자: "출력물이 너무 쉽게 파손돼"
{"message":"쉽게 부서진다고 하셨죠. 바닥 접착을 돕기 위해 브림을 켭니다.","actions":[{"type":"set_config","preset":"print","options":{"brim_width":5,"brim_type":"outer_only"}}]}

사용자: "잘 부서져"
{"message":"쉽게 부서진다고 하셨죠. 브림을 켜서 밑부분을 더 단단하게 붙이겠습니다.","actions":[{"type":"set_config","preset":"print","options":{"brim_width":5,"brim_type":"outer_only"}}]}

사용자: "공중에 떠서 나와요"
{"message":"공중으로 나오는 부분이 있군요. 받침대(서포트)를 켜겠습니다.","actions":[{"type":"set_config","preset":"print","options":{"enable_support":true}}]}

사용자: "안쪽을 더 단단하게"
{"message":"안쪽을 더 꽉 채우도록 채움을 22%로 올릴게요.","actions":[{"type":"set_config","preset":"print","options":{"sparse_infill_density":"22%"}}]}

사용자: "브림이 뭐예요?"
{"message":"브림은 맨 아래에 플라스틱을 조금 더 깔아 베드에 잘 붙게 하는 기능이에요.","actions":[]})OLLAMA";

    std::string prompt;
    if (ui_prefers_korean())
        prompt = kPromptKo;
    else
        prompt = kPromptEn;

    if (!apply_mode)
        prompt += ui_prefers_korean() ? kQuestionModeKo : kQuestionModeEn;
    return prompt;
}

std::string OllamaActionExecutor::build_context_json()
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

std::string OllamaActionExecutor::build_compact_context_json()
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

    for (const auto& action : root["actions"]) {
        if (!action.contains("type") || !action["type"].is_string())
            continue;

        const std::string type = action["type"].get<std::string>();
        if (type == "slice") {
            had_slice_action = true;
            deferred_slice   = action;
            continue;
        }

        if (type == "arrange" || type == "clone_selection" || type == "rotate" || type == "translate"
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

        OllamaActionResult result;
        if (type == "set_config")
            result = apply_set_config(action);
        else if (type == "ui_select_tab")
            result = apply_ui_select_tab(action);
        else if (type == "delete_selection")
            result = apply_delete_selection();
        else if (type == "clone_selection")
            result = apply_clone_selection();
        else if (type == "arrange")
            result = apply_arrange();
        else if (type == "save_project")
            result = apply_save_project(action);
        else if (type == "add_model")
            result = apply_add_model(action);
        else if (type == "menu_item")
            result = apply_menu_item(action);
        else if (type == "translate" || type == "rotate" || type == "scale")
            result = apply_transform(action, type.c_str());
        else
            result = OllamaActionResult{false, false, "Unknown action: " + type};

        if (type == "set_config" && result.effective_change)
            config_effective_change = true;
        if (!result.message.empty() && result.message.find("Skipped duplicate") == std::string::npos)
            OllamaTelemetry::action_executed(type, result.success, result.effective_change);
        results.push_back(std::move(result));
    }

    if (config_effective_change) {
        invalidate_context_cache();
        results.push_back(apply_slice(nlohmann::json::object({{"scope", "plate"}})));
        OllamaIntentContext::mark_pending_slice_feedback();
    } else if (had_slice_action) {
        results.push_back(apply_slice(deferred_slice));
    }

    return results;
}

}} // namespace
