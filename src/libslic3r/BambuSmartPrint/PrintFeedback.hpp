#ifndef slic3r_BambuSmartPrint_PrintFeedback_hpp_
#define slic3r_BambuSmartPrint_PrintFeedback_hpp_

#include "BambuSmartPrintTypes.hpp"

#include <string>
#include <vector>

namespace Slic3r {
namespace BambuSmartPrint {

/** What the user can say about a finished print. A print the printer reports
 *  as finished can still have gone wrong (lifted corners, stringing); these
 *  answers let the learning store count it as a failure of the right kind. */
struct PrintFeedbackIssue
{
    const char*     id;
    const char*     label;    // shown to the user (English msgid)
    FailureCategory category; // Unknown = recorded, but no setting is changed
};

class PrintFeedback
{
public:
    static const std::vector<PrintFeedbackIssue>& issues();
    static const PrintFeedbackIssue* find(const std::string& id);

    /** Why a learned setting adjustment exists, in plain words, from what the
     *  profile recorded ("the first layer came loose 2 times"). Empty when the
     *  key is not one learning adjusts or nothing explains it. */
    static std::string reason_for(const PrinterLearningProfile& p, const std::string& setting_key);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
