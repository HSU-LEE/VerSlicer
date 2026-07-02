#include "OllamaAssistRouter.hpp"

#include "OllamaConfig.hpp"

#include "../MakerWorld/MakerWorldSearchService.hpp"

namespace Slic3r { namespace GUI {

bool OllamaAssistRouter::should_use_assist_loop(const std::string& user_request, bool apply_mode)
{
    if (!apply_mode || user_request.empty() || !ollama_assist_loop_enabled())
        return false;
    if (MakerWorldSearchService::is_pure_makerworld_request(user_request))
        return false;
    return true;
}

bool OllamaAssistRouter::should_use_two_hop(const std::string& user_request, bool apply_mode)
{
    (void) user_request;
    (void) apply_mode;
    return false;
}

bool OllamaAssistRouter::should_apply_rule_only(const std::string& /*user_request*/, bool /*apply_mode*/)
{
    return false;
}

}} // namespace
