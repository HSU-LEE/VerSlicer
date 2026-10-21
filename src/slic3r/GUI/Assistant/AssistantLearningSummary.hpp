#ifndef slic3r_AssistantLearningSummary_hpp_
#define slic3r_AssistantLearningSummary_hpp_

#include "AssistantActionExecutor.hpp"

#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"

#include <nlohmann/json.hpp>

#include <string>

class wxWindow;

namespace Slic3r { namespace GUI {

/** "What VerSlicer learned about this printer": Smart Print's per-printer
 *  learning (counts, failure causes, setting adjustments) in plain words, so
 *  the user can see why suggestions change over time. */
class AssistantLearningSummary
{
public:
    /** Pure. {"prints", "succeeded", "success_rate", "facts": [..],
     *  "adjustments": [{key, what, change}], "empty": bool} — English text. */
    /** {"type":"report_print_result","result":"good"|issue id}: the user's verdict on the last print. */
    static AssistantActionResult report_result(const nlohmann::json& action);

    static nlohmann::json describe(const BambuSmartPrint::PrinterLearningProfile& p);

    /** Printer the learning applies to now (selected device, or "local"). */
    static std::string current_printer_id();

    /** Smart Print menu: show the summary for the current printer. */
    static void show_dialog(wxWindow* parent);

    /** Executor handler for the what_was_learned tool. */
    static AssistantActionResult apply(const nlohmann::json& action);
};

}} // namespace Slic3r::GUI

#endif
