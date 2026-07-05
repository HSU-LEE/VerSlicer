#ifndef slic3r_AIPipeline_PrintJobState_hpp_
#define slic3r_AIPipeline_PrintJobState_hpp_

namespace Slic3r { namespace GUI { namespace AIPipeline {

/**
 * States of the end-to-end AI print pipeline (Phase 3).
 *
 * The happy path advances monotonically:
 *   Idle -> IntentExtracted -> [Clarifying] -> Searching -> CandidateSelect ->
 *   Importing -> MeshHealth -> [Repairing] -> GeometryAnalysis -> AutoConfig ->
 *   Slicing -> Estimating -> ReadyToPrint -> [Sending] -> Done
 *
 * Terminal states are Done / Failed / Cancelled. Clarifying / Repairing / Sending
 * are optional and entered conditionally.
 */
enum class PrintJobState {
    Idle,
    IntentExtracted,
    Clarifying,
    Searching,
    CandidateSelect,
    Importing,
    MeshHealth,
    Repairing,
    GeometryAnalysis,
    AutoConfig,
    Slicing,
    Estimating,
    ReadyToPrint,
    Sending,
    Done,
    Failed,
    Cancelled,
};

/** Stable identifier string for logs / telemetry / UI status (never localized). */
inline const char* to_string(PrintJobState s)
{
    switch (s) {
    case PrintJobState::Idle:             return "Idle";
    case PrintJobState::IntentExtracted:  return "IntentExtracted";
    case PrintJobState::Clarifying:       return "Clarifying";
    case PrintJobState::Searching:        return "Searching";
    case PrintJobState::CandidateSelect:  return "CandidateSelect";
    case PrintJobState::Importing:        return "Importing";
    case PrintJobState::MeshHealth:       return "MeshHealth";
    case PrintJobState::Repairing:        return "Repairing";
    case PrintJobState::GeometryAnalysis: return "GeometryAnalysis";
    case PrintJobState::AutoConfig:       return "AutoConfig";
    case PrintJobState::Slicing:          return "Slicing";
    case PrintJobState::Estimating:       return "Estimating";
    case PrintJobState::ReadyToPrint:     return "ReadyToPrint";
    case PrintJobState::Sending:          return "Sending";
    case PrintJobState::Done:             return "Done";
    case PrintJobState::Failed:           return "Failed";
    case PrintJobState::Cancelled:        return "Cancelled";
    }
    return "Unknown";
}

/** True for the three terminal states; used to gate cleanup / re-entry. */
inline bool is_terminal(PrintJobState s)
{
    return s == PrintJobState::Done || s == PrintJobState::Failed || s == PrintJobState::Cancelled;
}

}}} // namespace Slic3r::GUI::AIPipeline

#endif
