#ifndef slic3r_PrintIntentExtractor_hpp_
#define slic3r_PrintIntentExtractor_hpp_

#include "PrintIntent.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

/**
 * Builds and enriches a PrintIntent. The deterministic layer defers entirely to
 * PrintGoalParser (no new keyword routing rules are introduced here).
 */
class PrintIntentExtractor
{
public:
    /** Deterministic extraction: PrintGoalParser::parse + priority derivation only. */
    static PrintIntent extract_deterministic(const std::string& user_text);

    /** Derive a high-level priority from the parsed symptom goal weights/intents. */
    static PrintIntentPriority derive_priority(const PrintGoal& goal);

    /** Fill material/printer slots from mesh + active config when still empty. */
    static void enrich_from_geometry(PrintIntent&              intent,
                                     const ModelAnalysis&      mesh,
                                     const DynamicPrintConfig& base_config,
                                     const std::string&        printer_id);

    /**
     * Merge LLM-extracted slots into an existing intent.
     * Deterministic symptom intents always win; material/priority/object_description are
     * overwritten only when LLM confidence >= 0.7; target_size is LLM-only.
     */
    static void merge_llm_slots(PrintIntent& intent, const nlohmann::json& slots);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
