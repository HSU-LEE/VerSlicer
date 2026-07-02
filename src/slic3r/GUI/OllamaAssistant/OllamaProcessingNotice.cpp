#include "OllamaProcessingNotice.hpp"

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

bool ui_is_korean()
{
    return wxGetApp().current_language_code().StartsWith("ko");
}

} // namespace

void OllamaProcessingNotice::show(Plater* plater, const std::string& text)
{
    if (!plater_notifications_available(plater) || text.empty())
        return;
    if (NotificationManager* nm = plater->get_notification_manager())
        nm->bbl_show_ollama_processing_notification(text);
}

void OllamaProcessingNotice::hide(Plater* plater)
{
    if (!plater_notifications_available(plater))
        return;
    if (NotificationManager* nm = plater->get_notification_manager())
        nm->bbl_close_ollama_processing_notification();
}

void OllamaProcessingNotice::show_thinking(Plater* plater)
{
    show(plater, ui_is_korean() ? std::string("요청을 처리하고 있습니다…") : std::string(_u8L("Working on your request…")));
}

void OllamaProcessingNotice::show_starting(Plater* plater)
{
    show(plater, ui_is_korean() ? std::string("AI를 시작하는 중…") : std::string(_u8L("Starting AI…")));
}

}} // namespace
