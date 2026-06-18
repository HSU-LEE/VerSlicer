#include "OllamaResponseNormalizer.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaConfig.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaIntentRules.hpp"
#include "OllamaTelemetry.hpp"

#include "../MakerWorld/MakerWorldSearchService.hpp"

namespace Slic3r { namespace GUI {

namespace {

using namespace OllamaIntentRules;

void prune_misleading_actions(nlohmann::json& root, const std::string& user_req)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;

    const bool quality_symptom = describes_print_quality_symptom(user_req);
    const bool placement       = contains_placement_intent(user_req);
    const bool allow_arrange   = placement && !quality_symptom;

    nlohmann::json kept = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object()) {
            kept.push_back(a);
            continue;
        }
        const std::string type = a.value("type", "");
        if (type == "delete_selection" && !user_wants_delete(user_req))
            continue;
        if (type == "arrange" && !allow_arrange)
            continue;
        if (type == "menu_item")
            continue;
        if (type == "ui_select_tab" && !a.contains("tab"))
            continue;
        kept.push_back(a);
    }
    root["actions"] = std::move(kept);
}

struct ActionScan
{
    bool has_set_config     = false;
    bool has_enable_support = false;
    bool has_brim           = false;
    bool disables_brim      = false;
    bool has_warp_opt       = false;
    bool has_strength_opt   = false;
    bool has_rotate         = false;
    bool has_retraction     = false;
    bool has_top            = false;
    bool has_first_layer    = false;
    bool has_arrange        = false;
    bool has_makerworld     = false;
};

static void scan_set_config_options(const nlohmann::json& action, ActionScan& scan)
{
    if (!action.contains("options") || !action["options"].is_object())
        return;
    const auto& options = action["options"];
    if (options.contains("enable_support"))
        scan.has_enable_support = true;
    if (options.contains("enable_brim") || options.contains("brim_width") || options.contains("brim"))
        scan.has_brim = true;
    if (options.contains("enable_brim") && options["enable_brim"].is_boolean()
        && !options["enable_brim"].get<bool>())
        scan.disables_brim = true;
    if (options.contains("brim_width") && options["brim_width"].is_number()
        && options["brim_width"].get<double>() == 0.0)
        scan.disables_brim = true;
    if (options.contains("elefant_foot_compensation"))
        scan.has_warp_opt = true;
    if (options.contains("sparse_infill_density") || options.contains("wall_loops"))
        scan.has_strength_opt = true;
    if (options.contains("retraction_length"))
        scan.has_retraction = true;
    if (options.contains("ironing_type") || options.contains("top_shell_layers"))
        scan.has_top = true;
    if (options.contains("initial_layer_print_height"))
        scan.has_first_layer = true;
}

static ActionScan scan_actions(const nlohmann::json& actions)
{
    ActionScan scan;
    if (!actions.is_array())
        return scan;
    for (const auto& action : actions) {
        if (!action.is_object())
            continue;
        const std::string type = action.value("type", "");
        if (type == "set_config") {
            scan.has_set_config = true;
            scan_set_config_options(action, scan);
        } else if (type == "rotate") {
            scan.has_rotate = true;
        } else if (type == "arrange") {
            scan.has_arrange = true;
        } else if (type == "makerworld_search" || type == "import_makerworld") {
            scan.has_makerworld = true;
        }
    }
    return scan;
}

static void inject_brim_action(nlohmann::json& root, ActionScan& scan, double brim_w)
{
    if (scan.has_brim)
        return;
    root["actions"].push_back({{"type", "set_config"},
                                 {"preset", "print"},
                                 {"options", {{"brim_width", brim_w}, {"brim_type", "outer_only"}}}});
    scan.has_brim = true;
}

static void patch_flip_rotation_axes(nlohmann::json& root, bool wants_flip)
{
    if (!wants_flip || !root.contains("actions") || !root["actions"].is_array())
        return;
    for (auto& a : root["actions"]) {
        if (!a.is_object())
            continue;
        const std::string type = a.value("type", "");
        if (type != "rotate")
            continue;
        if (!a.contains("x"))
            a["x"] = 180.0;
        if (!a.contains("y"))
            a["y"] = 0.0;
        if (!a.contains("z"))
            a["z"] = 0.0;
    }
}

/** Always inject rotate/arrange when the user asks — not gated on OLLAMA_KEYWORD_INJECT. */
static void ensure_geometry_actions_from_user_text(nlohmann::json& root, const std::string& user_req,
                                                   ActionScan& scan)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        root["actions"] = nlohmann::json::array();

    const bool quality_symptom = describes_print_quality_symptom(user_req);
    const bool wants_flip      = contains_flip_intent(user_req);
    const bool wants_rotate    = contains_rotate_intent(user_req);
    const bool wants_place     = contains_placement_intent(user_req) && !quality_symptom;

    patch_flip_rotation_axes(root, wants_flip);

    if (wants_flip && !scan.has_rotate) {
        root["actions"].push_back({{"type", "rotate"}, {"x", 180.0}, {"y", 0.0}, {"z", 0.0}});
        scan.has_rotate = true;
    }

    if (wants_rotate && !scan.has_rotate) {
        nlohmann::json rot = {{"type", "rotate"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
        if (wants_flip)
            rot["x"] = 180.0;
        else if (const auto z = parse_z_rotation_degrees(user_req))
            rot["z"] = *z;
        root["actions"].push_back(std::move(rot));
        scan.has_rotate = true;
    }

    if (wants_place && !scan.has_arrange)
        root["actions"].push_back({{"type", "arrange"}});
}

static bool user_lists_multiple_goals(const std::string& user)
{
    int goals = 0;
    if (contains_adhesion_intent(user) || contains_brim_intent(user))
        ++goals;
    if (contains_strength_intent(user) || contains_durability_intent(user))
        ++goals;
    if (contains_support_intent(user) || contains_midair_or_failure_intent(user))
        ++goals;
    if (user.find("빨리") != std::string::npos || user.find("slow") != std::string::npos
        || user.find("오래") != std::string::npos || user.find("fast") != std::string::npos)
        ++goals;
    return goals >= 2;
}

/** Keep at most 2 option keys per set_config unless user listed multiple goals. */
static void enforce_minimal_set_config(nlohmann::json& root, const std::string& user_req)
{
    if (!root.contains("actions") || !root["actions"].is_array() || user_lists_multiple_goals(user_req))
        return;

    auto priority_keys = [&]() -> std::vector<std::string> {
        if (contains_adhesion_intent(user_req) || contains_brim_intent(user_req))
            return {"brim_width", "brim_type", "enable_brim", "initial_layer_print_height",
                    "elefant_foot_compensation"};
        if (contains_durability_intent(user_req) || contains_strength_intent(user_req))
            return {"sparse_infill_density", "wall_loops", "top_shell_layers", "bottom_shell_layers"};
        if (contains_support_intent(user_req) || contains_midair_or_failure_intent(user_req))
            return {"enable_support"};
        if (OllamaIntentContext::user_wants_stringing_relief(user_req))
            return {"retraction_length"};
        if (OllamaIntentContext::user_wants_top_surface_quality(user_req))
            return {"layer_height", "top_shell_layers", "ironing_type"};
        return {};
    };

    const std::vector<std::string> priority = priority_keys();

    for (auto& a : root["actions"]) {
        if (!a.is_object() || a.value("type", "") != "set_config" || !a.contains("options"))
            continue;
        if (!a["options"].is_object() || a["options"].size() <= 2)
            continue;

        nlohmann::json trimmed = nlohmann::json::object();
        for (const std::string& key : priority) {
            if (!a["options"].contains(key))
                continue;
            trimmed[key] = a["options"][key];
            if (key == "brim_width" && a["options"].contains("brim_type"))
                trimmed["brim_type"] = a["options"]["brim_type"];
            if (trimmed.size() >= 2)
                break;
        }
        if (trimmed.empty()) {
            size_t kept = 0;
            for (auto it = a["options"].begin(); it != a["options"].end() && kept < 2; ++it, ++kept)
                trimmed[it.key()] = it.value();
        }
        if (trimmed.contains("brim_width") && a["options"].contains("brim_type") && !trimmed.contains("brim_type"))
            trimmed["brim_type"] = a["options"]["brim_type"];
        a["options"] = std::move(trimmed);
    }
}

} // namespace

void OllamaResponseNormalizer::drop_redundant_slice_actions(nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    const ActionScan scan = scan_actions(root["actions"]);
    if (!scan.has_set_config)
        return;
    nlohmann::json filtered = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (a.is_object() && a.value("type", "") == "slice")
            continue;
        filtered.push_back(a);
    }
    root["actions"] = std::move(filtered);
}

OllamaNormalizeResult OllamaResponseNormalizer::normalize(nlohmann::json& root, const std::string& user_req,
                                                          bool include_makerworld)
{
    OllamaNormalizeResult result;
    if (!root.contains("actions") || !root["actions"].is_array())
        root["actions"] = nlohmann::json::array();
    const size_t actions_before = root["actions"].size();

    if (include_makerworld && MakerWorldSearchService::is_pure_makerworld_request(user_req)) {
        ActionScan initial_scan = scan_actions(root["actions"]);
        if (!initial_scan.has_makerworld) {
            const std::string url = extract_first_url_from_text(user_req);
            if (!url.empty())
                root["actions"].push_back({{"type", "import_makerworld"}, {"url", url}});
            else
                root["actions"].push_back({
                    {"type", "makerworld_search"},
                    {"query", MakerWorldSearchService::normalize_search_query(user_req)},
                });
        }
    }

    if (include_makerworld && root["actions"].is_array()) {
        const std::string norm_q = MakerWorldSearchService::normalize_search_query(user_req);
        for (auto& a : root["actions"]) {
            if (!a.is_object())
                continue;
            if (a.value("type", "") != "makerworld_search")
                continue;
            const std::string existing = a.value("query", "");
            if (existing.empty() || MakerWorldSearchService::is_pure_makerworld_request(user_req))
                a["query"] = norm_q;
        }
    }

    prune_misleading_actions(root, user_req);

    const bool keyword_inject = ollama_keyword_inject_enabled();
    bool       wants_support = false;
    bool       wants_brim = false;
    bool       wants_durability = false;
    bool       wants_strength = false;
    bool       wants_no_brim = false;
    bool       vague_fix = false;
    bool       wants_warp = false;
    bool       slice_support = false;
    bool       wants_stringing = false;
    bool       wants_top = false;
    bool       wants_first_layer = false;
    bool       wants_lay_flat = false;
    bool       tall_narrow = false;
    double     recommended_brim_w = 5.0;

    if (keyword_inject) {
        wants_support    = contains_support_intent(user_req) || contains_midair_or_failure_intent(user_req);
        wants_brim       = contains_brim_intent(user_req) || contains_adhesion_intent(user_req);
        wants_durability = contains_durability_intent(user_req);
        wants_strength   = (contains_strength_intent(user_req) || wants_durability)
            && !contains_explicit_infill_intent(user_req);
        wants_no_brim  = contains_disable_brim_intent(user_req);
        vague_fix      = OllamaIntentContext::user_vague_fix_request(user_req);
        wants_warp     = OllamaIntentContext::user_wants_warp_relief(user_req);
        slice_support  = OllamaIntentContext::slice_suggests_support();
        wants_stringing = OllamaIntentContext::user_wants_stringing_relief(user_req);
        wants_top       = OllamaIntentContext::user_wants_top_surface_quality(user_req);
        wants_first_layer = OllamaIntentContext::user_wants_first_layer_help(user_req);
        wants_lay_flat  = OllamaIntentContext::user_wants_lay_flat(user_req);
        const OllamaSelectionFootprint footprint = OllamaIntentContext::current_selection_footprint();
        tall_narrow = footprint.valid
            && ollama_selection_is_tall_narrow(footprint.x_mm, footprint.y_mm, footprint.z_mm);
        recommended_brim_w = ollama_recommended_brim_width_mm(footprint.x_mm, footprint.y_mm);
    }

    {
        nlohmann::json filtered = nlohmann::json::array();
        for (const auto& a : root["actions"]) {
            if (!a.is_object()) {
                filtered.push_back(a);
                continue;
            }
            const std::string type = a.value("type", "");
            if (type == "save_project")
                continue;
            filtered.push_back(a);
        }
        root["actions"] = filtered;
    }

    for (auto& a : root["actions"]) {
        if (!a.is_object())
            continue;
        const std::string type = a.value("type", "");
        if (type == "set_config") {
            if (a.contains("preset") && a["preset"].is_string() && a["preset"].get<std::string>().empty())
                a["preset"] = "print";
            if (!keyword_inject || !a.contains("options") || !a["options"].is_object())
                continue;
            if (a["options"].empty() && wants_support)
                a["options"]["enable_support"] = true;
            if (a["options"].empty() && wants_brim) {
                a["options"]["brim_width"] = 5;
                a["options"]["brim_type"]  = "outer_only";
            }
        }
    }

    ActionScan scan = scan_actions(root["actions"]);

    if (keyword_inject) {
    if ((wants_support || (slice_support && (vague_fix || describes_print_quality_symptom(user_req))))
        && !scan.has_enable_support) {
        root["actions"].push_back(
            {{"type", "set_config"}, {"preset", "print"}, {"options", {{"enable_support", true}}}});
        scan.has_enable_support = true;
        scan.has_set_config     = true;
    }

    if (wants_brim)
        inject_brim_action(root, scan, recommended_brim_w);

    if ((vague_fix || wants_warp) && contains_adhesion_intent(user_req) && !wants_brim && !wants_no_brim)
        inject_brim_action(root, scan, recommended_brim_w);

    if (wants_warp && !scan.has_warp_opt) {
        root["actions"].push_back({{"type", "set_config"},
                                   {"preset", "print"},
                                   {"options", {{"elefant_foot_compensation", 0.1}}}});
        scan.has_warp_opt   = true;
        scan.has_set_config = true;
    }

    if (wants_strength && !scan.has_strength_opt) {
        nlohmann::json opts = nlohmann::json::object();
        if (wants_durability && !contains_brim_intent(user_req) && !contains_adhesion_intent(user_req))
            opts["wall_loops"] = 3;
        opts["sparse_infill_density"] = wants_durability ? "25%" : "22%";
        root["actions"].push_back({{"type", "set_config"}, {"preset", "print"}, {"options", std::move(opts)}});
        scan.has_strength_opt = true;
        scan.has_set_config   = true;
    }

    if (wants_no_brim && !scan.disables_brim) {
        root["actions"].push_back(
            {{"type", "set_config"}, {"preset", "print"}, {"options", {{"brim_width", 0}}}});
        scan.disables_brim  = true;
        scan.has_set_config = true;
    }

    if (tall_narrow && (wants_lay_flat || vague_fix) && !scan.has_rotate) {
        root["actions"].push_back({{"type", "rotate"}, {"x", 90.0}, {"y", 0.0}, {"z", 0.0}});
        scan.has_rotate = true;
        OllamaTelemetry::intent_signal_injected("lay_flat_rotate", "tall_narrow");
    }

    if (wants_stringing && !scan.has_retraction) {
        root["actions"].push_back({{"type", "set_config"},
                                     {"preset", "filament"},
                                     {"filament_index", OllamaIntentContext::clamp_filament_index(0, 16)},
                                     {"options", {{"retraction_length", 0.8}}}});
        scan.has_retraction = true;
        scan.has_set_config = true;
        OllamaTelemetry::intent_signal_injected("retraction_length", "stringing");
    }

    if (wants_top && !scan.has_top) {
        root["actions"].push_back({{"type", "set_config"},
                                     {"preset", "print"},
                                     {"options", {{"ironing_type", "top"}, {"top_shell_layers", 4}}}});
        scan.has_top        = true;
        scan.has_set_config = true;
        OllamaTelemetry::intent_signal_injected("top_surface", "ironing");
    }

    if (wants_first_layer && !scan.has_first_layer) {
        root["actions"].push_back({{"type", "set_config"},
                                     {"preset", "print"},
                                     {"options", {{"initial_layer_print_height", 0.24}}}});
        scan.has_first_layer = true;
        scan.has_set_config  = true;
        OllamaTelemetry::intent_signal_injected("initial_layer_print_height", "first_layer");
    }

    }

    {
        ActionScan geometry_scan = scan_actions(root["actions"]);
        ensure_geometry_actions_from_user_text(root, user_req, geometry_scan);
    }
    OllamaActionExecutor::augment_geometry_object_targets(root, user_req);
    enforce_minimal_set_config(root, user_req);

    prune_misleading_actions(root, user_req);
    if (root.contains("actions") && root["actions"].is_array()) {
        for (auto& a : root["actions"]) {
            if (!a.is_object() || a.value("type", "") != "set_config")
                continue;
            if (a.contains("options") && a["options"].is_object())
                OllamaIntentContext::refine_set_config_options(a["options"], user_req);
        }
    }
    if (keyword_inject)
        OllamaActionExecutor::augment_actions_from_user_text(root, user_req);
    drop_redundant_slice_actions(root);
    if (root["actions"].size() > actions_before)
        OllamaTelemetry::normalize_injected_action("actions");
    return result;
}

}} // namespace
