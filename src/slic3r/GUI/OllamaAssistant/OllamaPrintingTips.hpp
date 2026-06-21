#ifndef slic3r_OllamaPrintingTips_hpp_
#define slic3r_OllamaPrintingTips_hpp_

#include <nlohmann/json.hpp>
#include <string>

namespace Slic3r { namespace GUI {

/** Lesser-known 3D printing levers for LLM context (not hardcoded scenario scripts). */
class OllamaPrintingTips
{
public:
    /** Relevant pro tips for the user request (max ~8), Korean or English text. */
    static nlohmann::json tips_for_request(const std::string& user_request, bool korean, size_t limit = 8);
};

}} // namespace

#endif
