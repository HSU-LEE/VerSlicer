#ifndef slic3r_CrashReporter_hpp_
#define slic3r_CrashReporter_hpp_

#include <string>

class wxWindow;

namespace Slic3r { namespace GUI {

/** Local crash reports: a crash writes <data_dir>/crash/last_crash.txt, and the
 *  next launch offers to open a prefilled GitHub issue. Nothing is uploaded. */
class CrashReporter
{
public:
    /** Install signal and terminate handlers. Call once data_dir() is set. */
    static void install();

    /** If the previous run crashed, ask whether to report it. Main thread. */
    static void offer_report_from_last_run(wxWindow* parent);

    /** Issue body for a raw report: version, macOS, the report with the home
     *  folder replaced by "~", cut to fit a URL. Pure; exposed for tests. */
    static std::string issue_body(const std::string& report, const std::string& home_dir, size_t max_chars = 5000);
};

}} // namespace

#endif
