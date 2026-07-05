#ifndef slic3r_PrintIntentSession_hpp_
#define slic3r_PrintIntentSession_hpp_

#include "PrintIntent.hpp"

#include <nlohmann/json.hpp>

#include <mutex>
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

/**
 * Plate/chat-scoped, thread-safe accumulator of the current PrintIntent across turns.
 * Deterministic symptom intents are merged via PrintGoalParser; LLM slots and geometry
 * enrichment are layered on top.
 */
class PrintIntentSession
{
public:
    static PrintIntentSession& instance();

    /** Snapshot copy of the current intent (thread-safe). */
    PrintIntent intent() const;

    /** Merge a chat turn without geometry context (no clarifier recompute). */
    void merge_turn(const std::string& user_text, const nlohmann::json* llm_slots = nullptr);

    /** Merge a chat turn with geometry context; also enriches slots and runs the clarifier. */
    void merge_turn(const std::string&        user_text,
                    const ModelAnalysis&      mesh,
                    const DynamicPrintConfig& base,
                    const nlohmann::json*     llm_slots = nullptr);

    void clear();

private:
    PrintIntentSession() = default;

    void merge_turn_locked(const std::string& user_text, const nlohmann::json* llm_slots);

    mutable std::mutex m_mutex;
    PrintIntent        m_intent;
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
