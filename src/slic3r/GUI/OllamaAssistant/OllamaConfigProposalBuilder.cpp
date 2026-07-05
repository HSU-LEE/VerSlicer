#include "OllamaConfigProposalBuilder.hpp"

#include "OllamaConfigProposalCache.hpp"
#include "../BambuSmartPrint/PrintPlannerGui.hpp"
#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../Plater.hpp"

#include "libslic3r/BambuSmartPrint/PrintGoalSession.hpp"

namespace Slic3r { namespace GUI {

using namespace BambuSmartPrint;

ConfigProposal OllamaConfigProposalBuilder::build_for_turn(Plater* plater, const PrintIntent& intent,
                                                           bool korean)
{
    const PlateContext ctx = PrintPlannerGui::build_plate_context(plater);
    return build_from_context(plater, ctx, intent, korean);
}

ConfigProposal OllamaConfigProposalBuilder::build_from_context(Plater* plater, const PlateContext& ctx,
                                                               const PrintIntent& intent, bool korean)
{
    (void) korean; // Localization happens at proposal_to_context_json render time.

    ConfigProposal proposal = AutoConfigEngine::propose(intent, ctx);
    OllamaConfigProposalCache::instance().set(proposal);
    sync_plan_to_session(plater, proposal, intent);
    return proposal;
}

void OllamaConfigProposalBuilder::sync_plan_to_session(Plater* plater, const ConfigProposal& proposal,
                                                       const PrintIntent& intent)
{
    // Only mirror when there is an active plate; otherwise leave any existing plan untouched.
    if (!plater || plater->model().objects.empty())
        return;

    auto& svc = BambuSmartPrintService::instance();

    PrintPlan plan;
    plan.goal                    = intent.symptom_goal;
    plan.base_config             = proposal.base_config;
    plan.proposed_config         = proposal.proposed_config;
    plan.auto_result.changes     = proposal.changes;
    plan.auto_result.blocked_changes = proposal.blocked_changes;
    plan.risks                   = proposal.risks;
    plan.explanation             = proposal.explanation;
    plan.success_estimate        = proposal.success_estimate;
    plan.change_count            = proposal.changes.size();
    plan.from_llm                = false;
    plan.apply_policy            = PrintPlannerGui::default_apply_policy();

    // Reuse the assessment the plate context already refreshed this turn so the Smart
    // Print workflow view keeps its mesh/readiness/prediction fields consistent.
    plan.mesh       = svc.last_mesh_analysis();
    plan.readiness  = svc.last_readiness_report();
    plan.prediction = svc.last_prediction();

    plan.root = nlohmann::json{
        {"message", proposal.explanation.summary},
        {"actions", proposal.set_config_actions.is_array() ? proposal.set_config_actions
                                                           : nlohmann::json::array()},
    };

    PrintGoalSession::instance().set_goal(intent.symptom_goal);
    PrintGoalSession::instance().set_last_plan(plan);
}

}} // namespace
