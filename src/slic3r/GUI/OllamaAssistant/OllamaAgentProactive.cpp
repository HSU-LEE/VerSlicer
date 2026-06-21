#include "OllamaAgentProactive.hpp"

#include "../GUI.hpp"

#include "OllamaAgentEventBus.hpp"
#include "OllamaAgentStateService.hpp"
#include "OllamaModelLoadAdvisor.hpp"

#include "../AICoach/AICoachController.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"

namespace Slic3r { namespace GUI {

namespace {

bool agent_proactive_enabled()
{
    if (!wxGetApp().app_config)
        return false;
    const std::string mode = wxGetApp().app_config->get("ollama", "assistant_mode");
    return mode == "agent" || mode == "assist" || mode == "apply";
}

void on_agent_event(const OllamaAgentEvent& evt)
{
    if (!wxGetApp().initialized() || wxGetApp().is_closing())
        return;
    if (!agent_proactive_enabled())
        return;
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return;

    switch (evt.kind) {
    case OllamaAgentEventKind::ModelLoaded:
        OllamaModelLoadAdvisor::schedule_after_model_load(plater);
        break;
    case OllamaAgentEventKind::SliceDone:
        if (evt.payload.value("success", true))
            break;
        if (AICoachController::is_enabled_for_current_mode() && evt.payload.contains("summary")) {
            const std::string summary = evt.payload.value("summary", "");
            const nlohmann::json apply  = evt.payload.value("apply_root", nlohmann::json::object());
            AICoachController::instance().on_print_failure_hint(plater, summary, apply);
        }
        break;
    case OllamaAgentEventKind::ConfigApplied:
        if (evt.payload.contains("options") && evt.payload["options"].is_object()) {
            const auto mismatches = OllamaAgentStateService::verify_config_applied(evt.payload["options"]);
            if (!mismatches.empty())
                OllamaAgentEventBus::instance().publish(OllamaAgentEventKind::ActionFailed,
                                                          {{"reason", mismatches.front()}});
        }
        break;
    case OllamaAgentEventKind::ActionFailed:
        if (evt.payload.contains("reason")) {
            const std::string reason = evt.payload.value("reason", "");
            if (!reason.empty())
                show_info(plater, reason, _L("AI Assistant"));
        }
        break;
    default:
        break;
    }
}

} // namespace

void OllamaAgentProactive::install()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    OllamaAgentEventBus::instance().subscribe(on_agent_event);
}

}} // namespace
