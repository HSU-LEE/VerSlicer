#include "PrintIntentSession.hpp"
#include "PrintIntentExtractor.hpp"
#include "PrintIntentClarifier.hpp"
#include "PrintGoalParser.hpp"

#include <algorithm>

namespace Slic3r {
namespace BambuSmartPrint {

PrintIntentSession& PrintIntentSession::instance()
{
    static PrintIntentSession s;
    return s;
}

PrintIntent PrintIntentSession::intent() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_intent;
}

void PrintIntentSession::merge_turn_locked(const std::string& user_text, const nlohmann::json* llm_slots)
{
    // Self-heal: slot_sources must be an object; anything else (null/array from a
    // bad initializer or stale state) would make operator[]/value() below throw.
    if (!m_intent.slot_sources.is_object())
        m_intent.slot_sources = nlohmann::json::object();

    // 1. Deterministic extraction of this turn.
    const PrintIntent turn = PrintIntentExtractor::extract_deterministic(user_text);

    // 2. Merge symptom goals via the existing PrintGoalParser::merge.
    m_intent.symptom_goal = PrintGoalParser::merge(m_intent.symptom_goal, turn.symptom_goal);

    // Carry forward deterministic slot provenance and confidence.
    if (turn.slot_sources.is_object()) {
        for (auto it = turn.slot_sources.begin(); it != turn.slot_sources.end(); ++it)
            m_intent.slot_sources[it.key()] = it.value();
    }
    m_intent.confidence = std::max(m_intent.confidence, turn.confidence);

    // 3. Layer LLM-extracted slots on top (respects confidence gating internally).
    if (llm_slots != nullptr && !llm_slots->is_null())
        PrintIntentExtractor::merge_llm_slots(m_intent, *llm_slots);

    // 4. Recompute priority from the merged symptom goal, unless a high-confidence
    //    LLM value already claimed the slot.
    const std::string prio_source =
        m_intent.slot_sources.value(to_string(PrintIntentSlot::Priority), std::string{});
    if (prio_source != to_string(PrintIntentSource::LlmExtract)) {
        const PrintIntentPriority derived = PrintIntentExtractor::derive_priority(m_intent.symptom_goal);
        if (derived != PrintIntentPriority::Unknown) {
            m_intent.priority = derived;
            m_intent.slot_sources[to_string(PrintIntentSlot::Priority)] =
                to_string(PrintIntentSource::DeterministicParser);
        }
    }
}

void PrintIntentSession::merge_turn(const std::string& user_text, const nlohmann::json* llm_slots)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    merge_turn_locked(user_text, llm_slots);
}

void PrintIntentSession::merge_turn(const std::string&        user_text,
                                    const ModelAnalysis&      mesh,
                                    const DynamicPrintConfig& base,
                                    const nlohmann::json*     llm_slots)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    merge_turn_locked(user_text, llm_slots);
    PrintIntentExtractor::enrich_from_geometry(m_intent, mesh, base, m_intent.printer_id);
    PrintIntentClarifier::recompute_missing_slots(m_intent, mesh, base);
}

void PrintIntentSession::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_intent = PrintIntent{};
}

} // namespace BambuSmartPrint
} // namespace Slic3r
