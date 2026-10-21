#ifndef slic3r_AssistantPurposeProfiles_hpp_
#define slic3r_AssistantPurposeProfiles_hpp_

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** What the print is for, in the user's terms ("a toy", "a bracket", "a vase").
 *  Each purpose maps to a fixed, reviewed set of print settings, so the result
 *  is predictable instead of improvised by the model. */
struct PurposeProfile
{
    std::string    id;          // "decorative", "functional", ...
    std::string    title;       // English; translated through the catalog
    std::string    why;         // one line for the review dialog and chat
    nlohmann::json options;     // print preset key -> value
};

class AssistantPurposeProfiles
{
public:
    static const std::vector<PurposeProfile>& all();
    static std::optional<PurposeProfile>       find(const std::string& id);

    /** {"type":"set_config","preset":"print","options":{...},"purpose":id,"reason":why} */
    static std::optional<nlohmann::json> to_set_config(const std::string& id);

    /** Purpose ids for the tool schema enum. */
    static std::vector<std::string> ids();
};

}} // namespace Slic3r::GUI

#endif
