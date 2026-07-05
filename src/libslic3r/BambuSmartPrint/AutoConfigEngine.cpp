#include "AutoConfigEngine.hpp"
#include "PrintPlanner.hpp"
#include "PrintIntentClarifier.hpp"
#include "AutoSettingsEngine.hpp"
#include "ConfigSnapshot.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

const char* severity_to_string(RiskSeverity s)
{
    switch (s) {
    case RiskSeverity::High:   return "high";
    case RiskSeverity::Medium: return "medium";
    case RiskSeverity::Low:    return "low";
    case RiskSeverity::Info:
    default:                   return "info";
    }
}

std::string mesh_fingerprint(const ModelAnalysis& m)
{
    std::string blob;
    blob += std::to_string(std::llround(m.volume_mm3)) + ";";
    blob += std::to_string(std::llround(m.height_mm * 100.0)) + ";";
    blob += std::to_string(std::llround(m.max_xy_mm * 100.0)) + ";";
    blob += std::to_string(std::llround(m.overhang_face_ratio * 1000.0)) + ";";
    blob += std::to_string(std::llround(m.overhang_ratio * 1000.0)) + ";";
    blob += std::to_string(m.needs_brim ? 1 : 0);
    return std::to_string(std::hash<std::string>{}(blob));
}

std::string intent_hash(const PrintIntent& intent)
{
    std::string blob = intent.material + "|" + to_string(intent.priority) + "|";
    std::vector<int> intents;
    for (PrintGoalIntent i : intent.symptom_goal.intents)
        intents.push_back(static_cast<int>(i));
    std::sort(intents.begin(), intents.end());
    for (int i : intents)
        blob += std::to_string(i) + ",";
    return std::to_string(std::hash<std::string>{}(blob));
}

// Collect the merged set_config options from an actions array.
nlohmann::json collect_set_config_options(const nlohmann::json& actions)
{
    nlohmann::json options = nlohmann::json::object();
    if (!actions.is_array())
        return options;
    for (const auto& a : actions) {
        if (!a.is_object() || !a.contains("type"))
            continue;
        if (a["type"] != "set_config")
            continue;
        if (a.contains("options") && a["options"].is_object()) {
            for (auto it = a["options"].begin(); it != a["options"].end(); ++it)
                options[it.key()] = it.value();
        }
    }
    return options;
}

} // namespace

ConfigProposal AutoConfigEngine::propose(const PrintIntent& intent, const PlateContext& ctx)
{
    ConfigProposal proposal;
    proposal.base_config     = ctx.base_config;
    proposal.blocked_changes = ctx.auto_result.blocked_changes;
    proposal.success_estimate = ctx.prediction.success_rate;

    // 1. Start from the AutoSettingsEngine-derived proposed config (base + auto delta),
    //    then apply goal-driven patches via the shared PrintPlanner helper.
    DynamicPrintConfig proposed = ctx.proposed_config;
    PrintPlanner::apply_goal_patches(proposed, intent.symptom_goal, ctx.mesh);
    proposal.proposed_config = proposed;

    // 2. Combine auto-settings changes with the base-vs-proposed diff (same util
    //    plan_without_llm uses), de-duplicating by key.
    std::vector<SettingChange> all_changes = ctx.auto_result.changes;
    const std::vector<SettingChange> delta_changes = ConfigSnapshot::diff(ctx.base_config, proposed);
    for (const SettingChange& ch : delta_changes) {
        const bool dup = std::any_of(all_changes.begin(), all_changes.end(),
                                     [&ch](const SettingChange& e) { return e.key == ch.key; });
        if (!dup)
            all_changes.push_back(ch);
    }
    proposal.changes = all_changes;

    // 3. Build a delta config carrying only the changed options.
    DynamicPrintConfig delta;
    for (const SettingChange& ch : all_changes) {
        if (const ConfigOption* opt = proposed.option(ch.key))
            delta.set_key_value(ch.key, opt->clone());
    }
    proposal.delta = delta;

    // 4. Risks + explanation via existing PrintPlanner helpers.
    proposal.risks = PrintPlanner::build_risks(ctx);

    PrintPlan explain_plan;
    explain_plan.goal                     = intent.symptom_goal;
    explain_plan.readiness                = ctx.readiness;
    explain_plan.prediction               = ctx.prediction;
    explain_plan.mesh                     = ctx.mesh;
    explain_plan.base_config              = ctx.base_config;
    explain_plan.proposed_config          = proposed;
    explain_plan.auto_result              = ctx.auto_result;
    explain_plan.auto_result.changes      = all_changes;
    explain_plan.explanation.tradeoff_note = PrintPlanner::build_tradeoff_note(intent.symptom_goal);
    proposal.explanation                  = PrintPlanner::build_explanation(explain_plan);

    // 5. set_config actions from the change list.
    nlohmann::json actions =
        PrintPlanner::config_delta_to_actions(ctx.base_config, proposed, all_changes);
    if (!actions.is_array())
        actions = nlohmann::json::array();
    proposal.set_config_actions = std::move(actions);

    // 6. Geometry actions (orientation hint) via AutoSettingsEngine.
    const std::string orientation = AutoSettingsEngine::suggested_orientation_hint(ctx.mesh);
    proposal.geometry_actions = nlohmann::json::array();
    if (!orientation.empty()) {
        proposal.geometry_actions.push_back(nlohmann::json{
            {"type", "orientation_hint"},
            {"hint", orientation},
        });
    }

    // 7. Proposal identity.
    proposal.proposal_id = mesh_fingerprint(ctx.mesh) + "-" + intent_hash(intent);

    // 8. Gate on blocking missing slots: withhold config actions and surface a question.
    if (!intent.blocking_slots.empty()) {
        proposal.has_blocking_missing_slots = true;
        proposal.set_config_actions         = nlohmann::json::array();
        proposal.clarifying_question        = PrintIntentClarifier::next_question(intent, false);
    }

    return proposal;
}

nlohmann::json AutoConfigEngine::proposal_to_context_json(const ConfigProposal& proposal, bool korean)
{
    nlohmann::json out = nlohmann::json::object();
    out["policy"]          = "REFINE_ONLY";
    out["summary"]         = proposal.explanation.summary;
    out["tradeoff_note"]   = proposal.explanation.tradeoff_note;
    out["success_estimate"] = proposal.success_estimate;

    nlohmann::json changes = nlohmann::json::array();
    constexpr size_t kMaxChanges = 8;
    for (const SettingChange& ch : proposal.changes) {
        if (changes.size() >= kMaxChanges)
            break;
        changes.push_back(nlohmann::json{
            {"key", ch.key},
            {"old", ch.old_value},
            {"new", ch.new_value},
            {"reason", ch.reason},
        });
    }
    out["changes"]              = std::move(changes);
    out["proposed_set_config"]  = collect_set_config_options(proposal.set_config_actions);

    nlohmann::json risks = nlohmann::json::array();
    for (const PrintRisk& r : proposal.risks) {
        risks.push_back(nlohmann::json{
            {"label", r.label},
            {"detail", r.detail},
            {"severity", severity_to_string(r.severity)},
        });
    }
    out["risks"] = std::move(risks);

    if (proposal.clarifying_question.has_value()) {
        const ClarifyingQuestion& q = *proposal.clarifying_question;
        out["clarifying_question"] = nlohmann::json{
            {"slot", to_string(q.slot)},
            {"question", korean ? q.question_ko : q.question_en},
            {"blocks_config", q.blocks_config},
        };
    }

    return out;
}

ConfigProposal AutoConfigEngine::merge_llm_actions(const ConfigProposal& base, const nlohmann::json& llm_root)
{
    ConfigProposal result = base;
    if (!llm_root.is_object())
        return result;

    if (llm_root.contains("message") && llm_root["message"].is_string())
        result.explanation.summary = llm_root["message"].get<std::string>();

    // Blocking proposals never auto-apply LLM config actions.
    if (result.has_blocking_missing_slots)
        return result;

    nlohmann::json root = nlohmann::json::object();
    root["actions"] = result.set_config_actions.is_array() ? result.set_config_actions
                                                           : nlohmann::json::array();

    if (llm_root.contains("actions") && llm_root["actions"].is_array()
        && !llm_root["actions"].empty()) {
        for (const auto& a : llm_root["actions"])
            root["actions"].push_back(a);
    }

    PrintPlanner::dedupe_actions(root);
    result.set_config_actions = root.value("actions", nlohmann::json::array());
    return result;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
