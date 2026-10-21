#ifndef slic3r_AssistantFailureDoctor_hpp_
#define slic3r_AssistantFailureDoctor_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Where a printer error code points, and which print settings to inspect. */
struct AssistantFailureArea
{
    std::string              area;          // "ams_feeding", "toolhead", "motion_first_layer", ...
    std::string              summary_en;
    std::vector<std::string> setting_keys;  // empty when the fix is mechanical, not a setting
};

/** Failure doctor: turns the selected printer's error state into evidence the
 *  assistant can reason over ("my print failed" -> codes, messages, suspects). */
class AssistantFailureDoctor
{
public:
    /** Classify an HMS long code ("0700200000020001") or print_error ("0700-8010").
     *  Pure function; safe in headless tests. */
    static AssistantFailureArea area_for_code(const std::string& code);

    /** Selected printer's state: connection, print_status, stage, print_error and
     *  HMS alerts with readable messages and suspect areas. GUI thread only. */
    static nlohmann::json printer_diagnostics_json();
};

}} // namespace

#endif
