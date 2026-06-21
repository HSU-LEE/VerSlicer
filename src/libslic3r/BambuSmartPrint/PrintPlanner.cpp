#include "PrintPlanner.hpp"
#include "PrintGoalParser.hpp"
#include "PrintGoalSession.hpp"
#include "ConfigSnapshot.hpp"
#include "ConfigOptionRead.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <set>
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

static nlohmann::json set_config_action(const std::string& key, const nlohmann::json& value)
{
    nlohmann::json options = nlohmann::json::object({{key, value}});
    if (key == "enable_support" && value.is_boolean() && value.get<bool>())
        options["support_type"] = "normal(auto)";
    return nlohmann::json{
        {"type", "set_config"},
        {"preset", "print"},
        {"options", std::move(options)},
    };
}

static void set_opt(DynamicPrintConfig& cfg, const char* key, const std::string& val)
{
    if (ConfigOption* opt = cfg.option(key))
        opt->deserialize(val);
}

static void set_opt(DynamicPrintConfig& cfg, const char* key, double val)
{
    if (ConfigOption* opt = cfg.option(key))
        opt->deserialize(std::to_string(val));
}

static void set_opt(DynamicPrintConfig& cfg, const char* key, int val)
{
    if (ConfigOption* opt = cfg.option(key))
        opt->deserialize(std::to_string(val));
}

static void set_opt(DynamicPrintConfig& cfg, const char* key, bool val)
{
    if (ConfigOption* opt = cfg.option(key))
        opt->deserialize(val ? "1" : "0");
}

static void apply_goal_patches(DynamicPrintConfig& cfg, const PrintGoal& goal, const ModelAnalysis& mesh)
{
    const bool wants_strong   = goal.has_intent(PrintGoalIntent::Strong) || goal.has_intent(PrintGoalIntent::Outdoor);
    const bool wants_fast     = goal.has_intent(PrintGoalIntent::Fast);
    const bool wants_cosmetic = goal.has_intent(PrintGoalIntent::Cosmetic);
    const bool wants_adhesion = goal.has_intent(PrintGoalIntent::Adhesion);
    const bool wants_overhang = goal.has_intent(PrintGoalIntent::Overhang);

    if (wants_strong && !wants_fast) {
        if (!cfg.has("wall_loops") || cfg.opt_int("wall_loops") < 3)
            set_opt(cfg, "wall_loops", 3);
        const std::string infill = cfg.opt_serialize("sparse_infill_density");
        if (infill.empty() || infill == "15%" || infill == "10%" || infill == "5%")
            set_opt(cfg, "sparse_infill_density", "20%");
    }

    if (wants_fast && goal.weight_fast >= goal.weight_strong) {
        if (cfg.has("layer_height")) {
            const double lh = cfg.opt_float("layer_height");
            if (lh < 0.24)
                set_opt(cfg, "layer_height", 0.24);
        }
        if (cfg.has("sparse_infill_density")) {
            const std::string infill = cfg.opt_serialize("sparse_infill_density");
            if (infill.empty() || infill == "25%" || infill == "30%" || infill == "35%" || infill == "40%")
                set_opt(cfg, "sparse_infill_density", "15%");
        }
    }

    if (wants_cosmetic && !wants_fast) {
        if (cfg.has("layer_height")) {
            const double lh = cfg.opt_float("layer_height");
            if (lh > 0.16)
                set_opt(cfg, "layer_height", 0.16);
        }
        if (cfg.has("ironing_type"))
            set_opt(cfg, "ironing_type", std::string("top"));
    }

    if (wants_adhesion || mesh.needs_brim) {
        if (cfg.has("brim_type"))
            set_opt(cfg, "brim_type", std::string("outer_only"));
        if (cfg.has("brim_width")) {
            const double bw = cfg.opt_float("brim_width");
            if (bw < 5.0)
                set_opt(cfg, "brim_width", 5.0);
        }
    }

    const double oh = mesh.overhang_face_ratio > 0 ? mesh.overhang_face_ratio : mesh.overhang_ratio;
    if ((wants_overhang || oh >= 0.15) && cfg.has("enable_support"))
        set_opt(cfg, "enable_support", true);
}

static std::string action_key(const nlohmann::json& action)
{
    if (!action.is_object() || !action.contains("type"))
        return {};
    const std::string type = action["type"].get<std::string>();
    if (type == "set_config" && action.contains("options") && action["options"].is_object()) {
        std::string keys;
        for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
            if (!keys.empty())
                keys += ",";
            keys += it.key();
        }
        return "set_config:" + keys;
    }
    return type;
}

} // namespace

PrintGoal PrintPlanner::parse_goal(const std::string& user_text)
{
    return PrintGoalParser::parse(user_text);
}

void PrintPlanner::dedupe_actions(nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;

    nlohmann::json kept = nlohmann::json::array();
    std::map<std::string, nlohmann::json> set_config_merged;
    std::set<std::string> seen_non_config;

    for (const auto& a : root["actions"]) {
        if (!a.is_object() || !a.contains("type"))
            continue;
        const std::string type = a["type"].get<std::string>();
        if (type == "set_config" && a.contains("options") && a["options"].is_object()) {
            for (auto it = a["options"].begin(); it != a["options"].end(); ++it)
                set_config_merged[it.key()] = it.value();
            continue;
        }
        const std::string key = action_key(a);
        if (seen_non_config.count(key))
            continue;
        seen_non_config.insert(key);
        kept.push_back(a);
    }

    if (!set_config_merged.empty()) {
        nlohmann::json opts = nlohmann::json::object();
        for (const auto& kv : set_config_merged)
            opts[kv.first] = kv.second;
        kept.insert(kept.begin(),
                    nlohmann::json{{"type", "set_config"}, {"preset", "print"}, {"options", std::move(opts)}});
    }

    root["actions"] = std::move(kept);
}

std::vector<PrintRisk> PrintPlanner::build_risks(const PlateContext& ctx)
{
    std::vector<PrintRisk> risks;
    const ModelAnalysis& mesh = ctx.mesh;

    const double oh = mesh.overhang_face_ratio > 0 ? mesh.overhang_face_ratio : mesh.overhang_ratio;
    if (oh >= 0.15) {
        PrintRisk r;
        r.kind                      = PrintRiskKind::Overhang;
        r.label                     = "Steep overhangs";
        r.detail                    = "Overhangs may sag without support.";
        r.severity                  = oh >= 0.3 ? RiskSeverity::High : RiskSeverity::Medium;
        r.recommended               = RecommendedAction::EnableSupport;
        r.recommended_action_text   = "enable_support";
        r.action_json               = set_config_action("enable_support", true);
        risks.push_back(std::move(r));
    }

    if (mesh.tall_narrow || mesh.height_mm >= 150.0) {
        PrintRisk r;
        r.kind                    = PrintRiskKind::TallNarrow;
        r.label                   = "Tall or narrow model";
        r.detail                  = "May wobble; brim improves stability.";
        r.severity                = RiskSeverity::Medium;
        r.recommended             = RecommendedAction::AddBrim;
        r.recommended_action_text = "brim";
        r.action_json             = nlohmann::json::array({
            set_config_action("brim_type", "outer_only"),
            set_config_action("brim_width", 5),
        });
        risks.push_back(std::move(r));
    }

    if (mesh.min_xy_footprint_mm2 > 0 && mesh.min_xy_footprint_mm2 < 400.0) {
        PrintRisk r;
        r.kind                    = PrintRiskKind::SmallFootprint;
        r.label                   = "Small bed contact";
        r.detail                  = "Limited first-layer area increases knock-over risk.";
        r.severity                = RiskSeverity::Medium;
        r.recommended             = RecommendedAction::AddBrim;
        r.recommended_action_text = "brim";
        r.action_json             = set_config_action("brim_width", 5);
        risks.push_back(std::move(r));
    }

    if (ctx.readiness.filament_mismatch) {
        PrintRisk r;
        r.kind                    = PrintRiskKind::FilamentMismatch;
        r.label                   = "Filament mismatch";
        r.detail                  = "Loaded filament may not match model needs.";
        r.severity                = RiskSeverity::High;
        r.recommended             = RecommendedAction::None;
        r.recommended_action_text = "check_filament";
        risks.push_back(std::move(r));
    }

    if (ctx.readiness.score > 0.f && ctx.readiness.score < 55.f) {
        PrintRisk r;
        r.kind                    = PrintRiskKind::Adhesion;
        r.label                   = "Bed adhesion risk";
        r.detail                  = ctx.readiness.headline.empty() ? "First layer may not stick well."
                                                                   : ctx.readiness.headline;
        r.severity                = RiskSeverity::High;
        r.recommended             = RecommendedAction::AddBrim;
        r.recommended_action_text = "brim";
        r.action_json             = nlohmann::json::array({
            set_config_action("brim_type", "outer_only"),
            set_config_action("brim_width", 5),
        });
        risks.push_back(std::move(r));
    }

    for (const std::string& factor : ctx.prediction.risk_factors) {
        PrintRisk r;
        r.kind   = PrintRiskKind::General;
        r.label  = factor;
        r.detail = factor;
        r.severity = RiskSeverity::Low;
        risks.push_back(std::move(r));
    }

    return risks;
}

std::string PrintPlanner::build_tradeoff_note(const PrintGoal& goal)
{
    const bool strong = goal.has_intent(PrintGoalIntent::Strong);
    const bool fast   = goal.has_intent(PrintGoalIntent::Fast);
    if (strong && fast) {
        if (goal.weight_fast > goal.weight_strong)
            return "Prioritizing speed — strength may be slightly reduced.";
        return "Prioritizing strength — print time may increase.";
    }
    if (goal.has_intent(PrintGoalIntent::Cosmetic) && fast)
        return "Fine surface quality and fast printing conflict — leaning toward your stronger priority.";
    return {};
}

nlohmann::json PrintPlanner::config_delta_to_actions(const DynamicPrintConfig& base,
                                                     const DynamicPrintConfig& proposed,
                                                     const std::vector<SettingChange>& changes)
{
    (void) base;
    (void) proposed;
    nlohmann::json options = nlohmann::json::object();
    for (const SettingChange& ch : changes) {
        if (ch.new_value == "true" || ch.new_value == "on")
            options[ch.key] = true;
        else if (ch.new_value == "false" || ch.new_value == "off")
            options[ch.key] = false;
        else {
            bool is_num = !ch.new_value.empty();
            for (char c : ch.new_value) {
                if (c != '.' && c != '-' && !std::isdigit(static_cast<unsigned char>(c))) {
                    is_num = false;
                    break;
                }
            }
            if (is_num && ch.new_value.find('.') != std::string::npos)
                options[ch.key] = std::stod(ch.new_value);
            else if (is_num)
                options[ch.key] = std::stoi(ch.new_value);
            else
                options[ch.key] = ch.new_value;
        }
    }
    if (options.empty())
        return nlohmann::json::array();
    return nlohmann::json::array({nlohmann::json{{"type", "set_config"},
                                                 {"preset", "print"},
                                                 {"options", std::move(options)}}});
}

PrintExplanation PrintPlanner::build_explanation(const PrintPlan& plan)
{
    PrintExplanation ex;
    ex.tradeoff_note = plan.explanation.tradeoff_note.empty() ? build_tradeoff_note(plan.goal)
                                                              : plan.explanation.tradeoff_note;

    for (const SettingChange& ch : plan.auto_result.changes) {
        if (!ch.reason.empty())
            ex.change_reasons.push_back(ch.reason);
        else if (!ch.key.empty())
            ex.change_reasons.push_back(ch.key + ": " + ch.new_value);
    }

    if (plan.goal.has_intent(PrintGoalIntent::Strong))
        ex.expected_effects.push_back("Part should be stronger and less brittle.");
    if (plan.goal.has_intent(PrintGoalIntent::Fast))
        ex.expected_effects.push_back("Print should finish sooner.");
    if (plan.goal.has_intent(PrintGoalIntent::Cosmetic))
        ex.expected_effects.push_back("Surface quality should improve.");

    if (ex.summary.empty()) {
        if (!plan.auto_result.summary.empty())
            ex.summary = plan.auto_result.summary;
        else if (!plan.readiness.headline.empty())
            ex.summary = plan.readiness.headline;
        else
            ex.summary = "Suggested settings for your print goal.";
    } else {
        ex.summary = plan.explanation.summary;
    }

    return ex;
}

PrintPlan PrintPlanner::plan_without_llm(const PlateContext& ctx, const PrintGoal& goal)
{
    PrintPlan plan;
    plan.goal             = goal;
    plan.readiness        = ctx.readiness;
    plan.prediction       = ctx.prediction;
    plan.mesh             = ctx.mesh;
    plan.base_config      = ctx.base_config;
    plan.auto_result      = ctx.auto_result;
    plan.proposed_config  = ctx.proposed_config;
    plan.success_estimate = ctx.prediction.success_rate;
    plan.change_count     = ctx.change_count;
    plan.from_llm         = false;

    DynamicPrintConfig proposed = ctx.proposed_config;
    apply_goal_patches(proposed, goal, ctx.mesh);

    std::vector<SettingChange> all_changes = ctx.auto_result.changes;
    const auto                 delta_changes = ConfigSnapshot::diff(ctx.base_config, proposed);
    for (const SettingChange& ch : delta_changes) {
        bool dup = false;
        for (const SettingChange& existing : all_changes) {
            if (existing.key == ch.key) {
                dup = true;
                break;
            }
        }
        if (!dup)
            all_changes.push_back(ch);
    }

    plan.proposed_config = proposed;
    plan.change_count    = all_changes.size();
    plan.auto_result.changes = all_changes;

    nlohmann::json actions = config_delta_to_actions(ctx.base_config, proposed, all_changes);
    if (!actions.is_array())
        actions = nlohmann::json::array();

    plan.root = nlohmann::json::object();
    plan.explanation.tradeoff_note = build_tradeoff_note(goal);
    plan.explanation.summary       = ctx.auto_result.summary.empty()
        ? "Applying settings matched to your print goal."
        : ctx.auto_result.summary;
    plan.root["message"] = plan.explanation.summary;
    plan.root["actions"] = actions;

    plan.risks = build_risks(ctx);
    dedupe_actions(plan.root);
    plan.explanation = build_explanation(plan);
    if (!plan.explanation.tradeoff_note.empty() && plan.root.contains("message")) {
        plan.root["message"] = plan.explanation.summary + " " + plan.explanation.tradeoff_note;
    }

    return plan;
}

PrintPlan PrintPlanner::plan_from_assistant(const PlateContext& ctx, const PrintGoal& goal,
                                            const nlohmann::json& assistant_root)
{
    PrintPlan base = plan_without_llm(ctx, goal);
    if (!assistant_root.is_object())
        return base;

    PrintPlan plan = base;
    plan.from_llm  = true;

    if (assistant_root.contains("message") && assistant_root["message"].is_string())
        plan.root["message"] = assistant_root["message"];

    if (assistant_root.contains("actions") && assistant_root["actions"].is_array()
        && !assistant_root["actions"].empty()) {
        nlohmann::json merged = nlohmann::json::array();
        if (plan.root.contains("actions") && plan.root["actions"].is_array()) {
            for (const auto& a : plan.root["actions"])
                merged.push_back(a);
        }
        for (const auto& a : assistant_root["actions"])
            merged.push_back(a);
        plan.root["actions"] = std::move(merged);
    }

    dedupe_actions(plan.root);
    plan.explanation.summary = plan.root.value("message", base.explanation.summary);
    plan.explanation       = build_explanation(plan);
    return plan;
}

PrintPlan PrintPlanner::replan(const PlateContext& ctx, PrintGoalSession& session)
{
    if (!session.needs_replan(ctx) && session.has_last_plan())
        return session.last_plan();
    PrintPlan plan = plan_without_llm(ctx, session.goal());
    session.set_last_plan(plan);
    return plan;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
