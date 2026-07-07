#include "OllamaSendRouter.hpp"

#include "OllamaAssistRouter.hpp"
#include "OllamaUserFlow.hpp"

#include "../MakerWorld/MakerWorldIntent.hpp"
#include "../MakerWorld/MakerWorldSearchService.hpp"

namespace Slic3r { namespace GUI {

bool OllamaSendRouter::acquisition_gate_open(bool apply_mode, bool orchestrator_enabled, bool orchestrator_active)
{
    return apply_mode && orchestrator_enabled && !orchestrator_active;
}

bool OllamaSendRouter::is_acquisition_request(const std::string& user_utf8, bool plate_has_model)
{
    return OllamaUserFlow::is_acquisition_print_request(user_utf8, plate_has_model);
}

std::string OllamaSendRouter::acquisition_query(const std::string& user_utf8)
{
    std::string query = MakerWorldSearchService::normalize_search_query(user_utf8);
    if (query.empty())
        query = user_utf8;
    return query;
}

bool OllamaSendRouter::should_bypass_to_makerworld(const std::string& user_utf8)
{
    return MakerWorldIntent::is_pure_makerworld_request(user_utf8);
}

bool OllamaSendRouter::should_use_assist_loop(const std::string& user_utf8, bool apply_mode,
                                             bool plate_has_model)
{
    return OllamaAssistRouter::should_use_assist_loop(user_utf8, apply_mode, plate_has_model);
}

}} // namespace
