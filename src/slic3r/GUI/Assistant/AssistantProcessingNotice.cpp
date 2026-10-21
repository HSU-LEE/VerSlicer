#include "AssistantProcessingNotice.hpp"

#include "AiLocale.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../NotificationManager.hpp"
#include "../Plater.hpp"

namespace Slic3r { namespace GUI {

namespace {

bool plater_notifications_available(Plater* plater)
{
    if (!plater || !wxTheApp)
        return false;
    const GUI_App& app = wxGetApp();
    if (app.is_closing() || !app.initialized())
        return false;
    return true;
}

} // namespace

void AssistantProcessingNotice::show(Plater* plater, const std::string& text)
{
    if (!plater_notifications_available(plater) || text.empty())
        return;
    if (NotificationManager* nm = plater->get_notification_manager())
        nm->bbl_show_assistant_processing_notification(text);
}

void AssistantProcessingNotice::hide(Plater* plater)
{
    if (!plater_notifications_available(plater))
        return;
    if (NotificationManager* nm = plater->get_notification_manager())
        nm->bbl_close_assistant_processing_notification();
}

void AssistantProcessingNotice::show_thinking(Plater* plater)
{
    show(plater, _u8L("Working on your request…"));
}

void AssistantProcessingNotice::show_starting(Plater* plater)
{
    show(plater, _u8L("Starting AI…"));
}

}} // namespace
