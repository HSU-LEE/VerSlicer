#ifndef slic3r_AICoachTriggerPolicy_hpp_
#define slic3r_AICoachTriggerPolicy_hpp_

#include "AICoachTypes.hpp"

#include <cstdint>

namespace Slic3r { namespace GUI {

/** UI elements suppressed when a trigger fires or while its card is visible. */
enum class AICoachSuppression : uint32_t {
    None                        = 0,
    SliceProgressNotification   = 1u << 0,
    DailyTipsInSliceNotif       = 1u << 1,
    GcodeLegendAutoShow         = 1u << 2,
    BeginnerJourney             = 1u << 3,
    BeginnerTourEnqueue         = 1u << 4,
};

inline AICoachSuppression operator|(AICoachSuppression a, AICoachSuppression b)
{
    return static_cast<AICoachSuppression>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool suppression_active(AICoachSuppression mask, AICoachSuppression flag)
{
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(flag)) != 0;
}

struct AICoachTriggerPolicy {
    AICoachImportance default_importance{ AICoachImportance::Normal };
    /** 0 = no dedup cooldown for this trigger. */
    int               dedup_ms{ 120000 };
    /** Default auto-dismiss; 0 keeps card until user acts. */
    int               default_auto_dismiss_ms{ 10000 };
    /** Applied once when the trigger event fires (e.g. slice complete). */
    AICoachSuppression suppresses_on_event{ AICoachSuppression::None };
    /** Applied while the card is on screen. */
    AICoachSuppression suppresses_while_active{ AICoachSuppression::None };

    static const AICoachTriggerPolicy& get(AICoachTriggerId id);
    static bool                        uses_dedup(AICoachTriggerId id);
    static void                        apply_defaults(AICoachCard& card);
};

}} // namespace

#endif
