#include "AICoachTriggerPolicy.hpp"

namespace Slic3r { namespace GUI {

namespace {

const AICoachTriggerPolicy kPolicies[] = {
    // None
    { AICoachImportance::Normal, 0, 10000, AICoachSuppression::None, AICoachSuppression::None },
    // ModelTallBrim
    { AICoachImportance::Normal, 120000, 10000, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
    // SliceDoneSend
    { AICoachImportance::Normal, 120000, 10000,
      AICoachSuppression::SliceProgressNotification | AICoachSuppression::DailyTipsInSliceNotif
          | AICoachSuppression::GcodeLegendAutoShow,
      AICoachSuppression::BeginnerJourney | AICoachSuppression::BeginnerTourEnqueue },
    // OverhangSupport
    { AICoachImportance::Normal, 120000, 10000, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
    // BedArrange
    { AICoachImportance::Low, 120000, 10000, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
    // AdhesionRisk
    { AICoachImportance::Critical, 120000, 0, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
    // PrintFailure (legacy alias; prefer FailureDoctor)
    { AICoachImportance::Critical, 0, 0, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
    // SendGateBlocked
    { AICoachImportance::Normal, 0, 12000, AICoachSuppression::None, AICoachSuppression::None },
    // PrintSuccessFinishing
    { AICoachImportance::Normal, 120000, 0, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
    // AppliedUndo
    { AICoachImportance::Normal, 120000, 15000, AICoachSuppression::None, AICoachSuppression::None },
    // PrintMonitor
    { AICoachImportance::Low, 300000, 8000, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
    // FailureDoctor
    { AICoachImportance::Critical, 0, 0, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
    // PersonalTrainer
    { AICoachImportance::Low, 300000, 12000, AICoachSuppression::None, AICoachSuppression::BeginnerJourney },
};

static_assert(sizeof(kPolicies) / sizeof(kPolicies[0]) == static_cast<size_t>(AICoachTriggerId::AppliedUndo) + 1,
              "AICoachTriggerPolicy table out of sync with AICoachTriggerId");

const AICoachTriggerPolicy kDefaultPolicy{ AICoachImportance::Normal, 120000, 10000,
                                           AICoachSuppression::None, AICoachSuppression::None };

} // namespace

const AICoachTriggerPolicy& AICoachTriggerPolicy::get(AICoachTriggerId id)
{
    const size_t idx = static_cast<size_t>(id);
    if (idx >= sizeof(kPolicies) / sizeof(kPolicies[0]))
        return kDefaultPolicy;
    return kPolicies[idx];
}

bool AICoachTriggerPolicy::uses_dedup(AICoachTriggerId id)
{
    return get(id).dedup_ms > 0;
}

void AICoachTriggerPolicy::apply_defaults(AICoachCard& card)
{
    const AICoachTriggerPolicy& policy = get(card.trigger);
    if (card.importance == AICoachImportance::Normal)
        card.importance = policy.default_importance;
    if (card.kind != AICoachCardKind::ExplainableRecommendation)
        card.auto_dismiss_ms = policy.default_auto_dismiss_ms;
    else if (policy.default_auto_dismiss_ms == 0)
        card.auto_dismiss_ms = 0;
}

}} // namespace
