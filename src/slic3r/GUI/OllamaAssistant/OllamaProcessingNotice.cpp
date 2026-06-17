#include "OllamaProcessingNotice.hpp"

#include "../NotificationManager.hpp"
#include "../Plater.hpp"

namespace Slic3r { namespace GUI {

void OllamaProcessingNotice::show(Plater* plater, const std::string& text)
{
    if (!plater || text.empty())
        return;
    if (NotificationManager* nm = plater->get_notification_manager())
        nm->bbl_show_ollama_processing_notification(text);
}

void OllamaProcessingNotice::hide(Plater* plater)
{
    if (!plater)
        return;
    if (NotificationManager* nm = plater->get_notification_manager())
        nm->bbl_close_ollama_processing_notification();
}

}} // namespace
