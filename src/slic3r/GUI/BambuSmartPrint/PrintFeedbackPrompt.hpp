#ifndef slic3r_PrintFeedbackPrompt_hpp_
#define slic3r_PrintFeedbackPrompt_hpp_

#include "libslic3r/PrintConfig.hpp"

#include <string>

class wxWindow;

namespace Slic3r { namespace GUI {

/** After a print the printer reports as finished, ask the user how it came
 *  out and feed the answer to the printer's learning profile. */
class PrintFeedbackPrompt
{
public:
    /** Notification with a "How did it come out?" link that opens ask(). */
    static void offer(const std::string& printer_id, const std::string& job_name, const DynamicPrintConfig& config);

    /** Ask, record the answer, and say what will change next time. */
    static void ask(wxWindow* parent, const std::string& printer_id, const std::string& job_name,
                    const DynamicPrintConfig& config);
};

}} // namespace Slic3r::GUI

#endif
