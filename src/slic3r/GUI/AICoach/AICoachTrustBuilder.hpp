#ifndef slic3r_AICoachTrustBuilder_hpp_
#define slic3r_AICoachTrustBuilder_hpp_

#include "AICoachTypes.hpp"

namespace Slic3r { namespace GUI {

class Plater;

struct AICoachTrustBuilder
{
    /** Fills trust brief + section lines from apply_root and current plate state. */
    static bool enrich_recommendation_card(AICoachCard& card, Plater* plater);
};

}} // namespace

#endif
