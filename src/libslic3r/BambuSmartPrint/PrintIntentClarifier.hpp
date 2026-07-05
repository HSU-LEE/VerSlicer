#ifndef slic3r_PrintIntentClarifier_hpp_
#define slic3r_PrintIntentClarifier_hpp_

#include "PrintIntent.hpp"

#include <optional>
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

struct ClarifyingQuestion {
    PrintIntentSlot slot{ PrintIntentSlot::Material };
    std::string     question_ko;
    std::string     question_en;
    bool            blocks_config{ false };
};

/**
 * Determines which intent slots are missing / blocking and produces at most one
 * clarifying question. Purely deterministic and headless.
 */
class PrintIntentClarifier
{
public:
    /** Recompute intent.missing_slots and intent.blocking_slots in place. */
    static void recompute_missing_slots(PrintIntent&              intent,
                                        const ModelAnalysis&      mesh,
                                        const DynamicPrintConfig& base);

    /** Return at most one blocking question (Material > Priority), or nullopt. */
    static std::optional<ClarifyingQuestion> next_question(const PrintIntent& intent, bool korean);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
