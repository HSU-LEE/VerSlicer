#include "OllamaActionExecutor.hpp"

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
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/menu.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr double kPi = 3.14159265358979323846;

Preset::Type preset_type_from_string(const std::string& s)
{
    if (s == "filament")
        return Preset::TYPE_FILAMENT;
    if (s == "printer")
        return Preset::TYPE_PRINTER;
    return Preset::TYPE_PRINT;
}

static std::string normalize_config_key(std::string key)
{
    boost::algorithm::trim(key);
    // Common aliases from LLMs / users.
    if (key == "support_material")
        return "enable_support";
    if (key == "support")
        return "enable_support";
    return key;
}

DynamicPrintConfig* edited_config(Preset::Type type)
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return nullptr;
    switch (type) {
    case Preset::TYPE_FILAMENT: return &bundle->filaments.get_edited_preset().config;
    case Preset::TYPE_PRINTER:  return &bundle->printers.get_edited_preset().config;
    default:                    return &bundle->prints.get_edited_preset().config;
    }
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
        nlohmann::json m;
        m["menu"] = mb->GetMenuLabelText(mi).utf8_string();
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

OllamaActionResult apply_set_config(const nlohmann::json& action)
{
    OllamaActionResult result;
    if (!wxGetApp().plater()) {
        result.message = "Plater not available";
        return result;
    }

    const Preset::Type ptype = preset_type_from_string(action.value("preset", "print"));
    DynamicPrintConfig* cfg  = edited_config(ptype);
    if (!cfg) {
        result.message = "Preset bundle not available";
        return result;
    }

    if (!action.contains("options") || !action["options"].is_object()) {
        result.message = "set_config: missing options object";
        return result;
    }

    ConfigSubstitutionContext ctxt{ForwardCompatibilitySubstitutionRule::Disable};
    int attempted = 0;
    int applied   = 0;
    std::string errors;
    std::vector<std::string> applied_kvs;
    for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
        const std::string raw_key = it.key();
        const std::string key     = normalize_config_key(raw_key);
        ++attempted;
        if (!cfg->has(key)) {
            if (!errors.empty())
                errors += "; ";
            errors += "unknown option: " + raw_key;
            continue;
        }
        std::string val = normalize_config_value(json_value_to_config_string(it.value()), key);
        bool ok = cfg->set_deserialize_nothrow(key, val, ctxt, false);
        if (!ok && key == "sparse_infill_pattern" && it.value().is_string()) {
            const std::string mapped = normalize_sparse_infill_pattern(val);
            if (mapped != val) {
                val = mapped;
                ok = cfg->set_deserialize_nothrow(key, val, ctxt, false);
            }
        }
        if (!ok) {
            if (!errors.empty())
                errors += "; ";
            errors += "failed: " + key;
            continue;
        }
        ++applied;
        // Read back the actually stored value for debugging/UX.
        std::string stored = val;
        try {
            stored = cfg->opt_serialize(key);
        } catch (...) {
        }
        applied_kvs.push_back(key + "=" + stored);
    }

    if (applied > 0) {
        if (Tab* tab = wxGetApp().get_tab(ptype)) {
            tab->update_dirty();
            tab->reload_config();
        }
        // Propagate config changes to plater and refresh sidebar UI.
        if (auto* plater = wxGetApp().plater()) {
            plater->on_config_change(wxGetApp().preset_bundle->full_config(false));
            plater->sidebar().update_presets(ptype);
            // Some UI paths (Process panel) refresh via mainframe helpers.
            if (wxGetApp().mainframe) {
                wxGetApp().mainframe->update_side_preset_ui();
            }
        }
    }

    result.success = applied > 0;
    std::string details;
    if (!applied_kvs.empty()) {
        details = " [";
        for (size_t i = 0; i < applied_kvs.size(); ++i) {
            if (i) details += ", ";
            details += applied_kvs[i];
        }
        details += "]";
    }
    result.message = (boost::format("Updated %1%/%2% setting(s)%3%%4%")
                      % applied % attempted
                      % (errors.empty() ? "" : " (" + errors + ")")
                      % details)
                         .str();
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
    if (sel.is_empty()) {
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
        result.success = true;
        result.message = (boost::format("Moved selection by (%1%, %2%, %3%) mm") % x % y % z).str();
    } else if (std::string(type_name) == "rotate") {
        const double rx = action.value("x", 0.0) * kPi / 180.0;
        const double ry = action.value("y", 0.0) * kPi / 180.0;
        const double rz = action.value("z", 0.0) * kPi / 180.0;
        sel.rotate(Vec3d(rx, ry, rz), tt);
        plater->take_snapshot("AI Assistant: Rotate", UndoRedo::SnapshotType::GizmoAction);
        canvas->do_rotate("");
        result.success = true;
        result.message = (boost::format("Rotated selection (deg: %1%, %2%, %3%)")
                          % action.value("x", 0.0) % action.value("y", 0.0) % action.value("z", 0.0))
                             .str();
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
        result.success = true;
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
    result.success = true;
    result.message = label;
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
    return apply_plater_event(EVT_GLTOOLBAR_CLONE, "Cloned selection");
}

OllamaActionResult apply_arrange()
{
    return apply_plater_event(EVT_GLTOOLBAR_ARRANGE, "Auto-arrange requested");
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
    if (!mf->open_menubar_item(wxString::FromUTF8(menu), wxString::FromUTF8(item))) {
        result.message = "Menu item not found: " + key;
        return result;
    }
    result.success = true;
    result.message = "Menu: " + key;
    return result;
}

} // namespace

std::string OllamaActionExecutor::build_system_prompt()
{
    return R"OLLAMA(You are Verslicer's AI assistant embedded in a 3D printing slicer.
You MUST respond with a single JSON object only. No markdown, no explanations outside JSON.
If you made a mistake, fix it and output corrected JSON. Never output validation errors as plain text.

Schema:

{
  "message": "Short explanation for the user (can be Korean or English)",
  "actions": [
    {
      "type": "set_config",
      "preset": "print",
      "options": { "layer_height": 0.2, "wall_loops": 3, "sparse_infill_density": "15%" }
    },
    {
      "type": "ui_select_tab",
      "tab": "prepare"
    },
    {
      "type": "slice",
      "scope": "plate"
    },
    {
      "type": "delete_selection"
    },
    {
      "type": "clone_selection"
    },
    {
      "type": "arrange"
    },
    {
      "type": "add_model",
      "path": "/absolute/path/to/model.stl"
    },
    {
      "type": "menu_item",
      "menu": "File",
      "item": "Export G-code"
    },
    {
      "type": "translate",
      "x": 10, "y": 0, "z": 0
    },
    {
      "type": "rotate",
      "x": 0, "y": 0, "z": 45
    },
    {
      "type": "scale",
      "factor": 1.2,
      "uniform": true
    }
  ]
}

Rules:
- "preset" for set_config: print, filament, or printer.
- Use exact option keys from the context (e.g. layer_height, sparse_infill_density, wall_loops, enable_support).
- Use numbers for numeric settings (e.g. layer_height: 0.2, wall_loops: 3).
- Percent values as strings with % (e.g. "20%").
- ui_select_tab: tab is one of "prepare", "preview", "monitor", "smart_print", "home".
- slice: scope is "plate" or "all".
- delete_selection: deletes selected object(s) on the plate.
- clone_selection: duplicates selected object(s) on the plate.
- arrange: auto-arrange objects on the plate.
- add_model: path must be an absolute file path (stl/3mf/obj/step...).
- menu_item: use only when needed; menu and item must match existing UI wording.
- translate/rotate/scale apply to the current selection on the build plate (mm, degrees, scale factor).
- If you only need to answer a question, use "actions": [].
- Do not invent option keys not listed in context.
- Never use delete_selection unless the user explicitly asked to delete/remove something.
- If the user describes a print failure or problem (e.g. mid-air printing, failed output), explain briefly and propose helpful "actions" (e.g. enable_support, brim, re-orient) when appropriate — never delete_selection unless they asked to delete.
- Never use add_model unless the user gave a file path or asked to import/load a model.
- Never use menu_item for File/save/export/quit unless the user explicitly asked for that workflow.
- Prefer one focused set_config per request; combine related keys in a single action.
- After set_config is applied, Verslicer automatically re-slices the current plate; do not add a separate slice action unless the user only asked to slice without changing settings.
- For transforms, only act when the user asked to move/rotate/scale/flip/arrange; use realistic values.)OLLAMA";
}

std::string OllamaActionExecutor::build_context_json()
{
    nlohmann::json ctx;
    auto* bundle = wxGetApp().preset_bundle;
    Plater* plater = wxGetApp().plater();

    if (bundle) {
        const DynamicPrintConfig& print_cfg = bundle->prints.get_edited_preset().config;
        static const char* kPrintKeys[]     = {
            "layer_height", "line_width", "sparse_infill_density", "sparse_infill_pattern",
            "wall_loops", "top_shell_layers", "bottom_shell_layers", "enable_support",
            "brim_width", "outer_wall_speed", "sparse_infill_speed", "initial_layer_print_height",
        };
        nlohmann::json print_opts = nlohmann::json::object();
        for (const char* key : kPrintKeys) {
            if (print_cfg.has(key))
                print_opts[key] = print_cfg.opt_serialize(key);
        }
        ctx["print_preset"]  = bundle->prints.get_edited_preset().name;
        ctx["print_options"] = print_opts;

        ctx["filament_preset"] = bundle->filaments.get_edited_preset().name;
        ctx["printer_preset"]  = bundle->printers.get_edited_preset().name;
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

    ctx["menus"] = build_menu_context_json();
    ctx["allowed_config_keys"] = nlohmann::json::array({
        "layer_height", "initial_layer_print_height", "line_width", "sparse_infill_density",
        "sparse_infill_pattern", "wall_loops", "top_shell_layers", "bottom_shell_layers",
        "enable_support", "brim_width", "outer_wall_speed", "sparse_infill_speed",
        "support_type", "support_on_build_plate_only", "raft_layers",
    });
    return ctx.dump(2);
}

nlohmann::json OllamaActionExecutor::extract_action_json(const std::string& assistant_text)
{
    std::string text = assistant_text;
    boost::trim(text);

    const std::string fence = "```json";
    const auto pos          = text.find(fence);
    if (pos != std::string::npos) {
        const auto start = pos + fence.size();
        const auto end   = text.find("```", start);
        if (end != std::string::npos)
            text = text.substr(start, end - start);
    } else {
        const auto p2 = text.find("```");
        if (p2 != std::string::npos) {
            const auto start = text.find('\n', p2);
            const auto end   = text.find("```", start == std::string::npos ? p2 + 3 : start);
            if (start != std::string::npos && end != std::string::npos)
                text = text.substr(start + 1, end - start - 1);
        }
    }
    boost::trim(text);

    // Recover the first balanced JSON object from a messy response.
    const auto brace = text.find('{');
    if (brace == std::string::npos)
        return nlohmann::json::parse(text);

    bool in_str = false;
    bool esc    = false;
    int  depth  = 0;
    size_t start = brace;
    size_t end   = std::string::npos;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (esc) { esc = false; continue; }
        if (c == '\\' && in_str) { esc = true; continue; }
        if (c == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth < 0)
                break;
            if (depth == 0) { end = i + 1; break; }
        }
    }
    if (end != std::string::npos && depth == 0)
        return nlohmann::json::parse(text.substr(start, end - start));

    throw std::runtime_error("No balanced JSON object found in assistant response");
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

    bool config_applied = false;
    bool had_slice_action = false;
    nlohmann::json deferred_slice = nlohmann::json::object({{"scope", "plate"}});

    for (const auto& action : root["actions"]) {
        if (!action.contains("type") || !action["type"].is_string())
            continue;

        const std::string type = action["type"].get<std::string>();
        if (type == "slice") {
            had_slice_action = true;
            deferred_slice = action;
            continue;
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
            result = OllamaActionResult{false, "Unknown action: " + type};

        if (type == "set_config" && result.success)
            config_applied = true;
        results.push_back(std::move(result));
    }

    // Settings changes require a fresh slice before preview/print.
    if (config_applied) {
        results.push_back(apply_slice(nlohmann::json::object({{"scope", "plate"}})));
    } else if (had_slice_action) {
        results.push_back(apply_slice(deferred_slice));
    }

    return results;
}

}} // namespace
