#ifndef slic3r_OllamaConfigProposalBuilder_hpp_
#define slic3r_OllamaConfigProposalBuilder_hpp_

#include "libslic3r/BambuSmartPrint/AutoConfigEngine.hpp"
#include "libslic3r/BambuSmartPrint/PrintIntent.hpp"
#include "libslic3r/BambuSmartPrint/PrintPlannerTypes.hpp"

namespace Slic3r { namespace GUI {

class Plater;

/**
 * Bridges the headless AutoConfigEngine into the GUI assist loop: derives a concrete
 * ConfigProposal from the current plate geometry + accumulated PrintIntent, caches it
 * for context injection, and keeps the Smart Print last-plan consistent.
 */
class OllamaConfigProposalBuilder
{
public:
    /**
     * Build a proposal for this turn from the live plate context and the merged intent.
     * Result is stored in OllamaConfigProposalCache and mirrored into PrintGoalSession.
     */
    static BambuSmartPrint::ConfigProposal build_for_turn(Plater* plater,
                                                          const BambuSmartPrint::PrintIntent& intent,
                                                          bool korean);

    /**
     * Same as build_for_turn but reuses an already-built PlateContext to avoid a second
     * (potentially expensive) plate assessment when the caller already has one.
     */
    static BambuSmartPrint::ConfigProposal build_from_context(Plater* plater,
                                                              const BambuSmartPrint::PlateContext& ctx,
                                                              const BambuSmartPrint::PrintIntent& intent,
                                                              bool korean);

    /** Mirror a proposal into the PrintGoalSession last-plan so Smart Print stays in sync. */
    static void sync_plan_to_session(Plater* plater, const BambuSmartPrint::ConfigProposal& proposal,
                                     const BambuSmartPrint::PrintIntent& intent);
};

}} // namespace

#endif
