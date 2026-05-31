#include "OllamaModelLoadAdvisor.hpp"

#include "OllamaActionExecutor.hpp"
#include "OllamaActionValidator.hpp"
#include "OllamaActionWorkflow.hpp"
#include "OllamaProcessingNotice.hpp"
#include "OllamaClient.hpp"
#include "OllamaServerManager.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../NotificationManager.hpp"
#include "../Plater.hpp"
#include "../format.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"

#include <atomic>
#include <memory>
#include <wx/timer.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kDefaultModel = "llama3.2";
constexpr const char* kDefaultHost  = "http://127.0.0.1:11434";

constexpr const char* kModelLoadUserPrompt = R"(A 3D model was just loaded onto the build plate.
Review the model geometry and current print settings in the context.
Recommend practical print settings and placement/orientation when helpful.
Allowed action types: set_config (preset print), translate, rotate, clone_selection (copy duplicate), arrange.
Use only keys listed in allowed_config_keys for set_config.
Consider layer_height, sparse_infill_density, wall_loops, enable_support, brim_width, and support settings when the model likely needs them.
If the model is tall/narrow, suggest a small rotate (e.g. lay flat). If it may not fit the bed, suggest translate to center or arrange.
Use conservative values: translate within ±50 mm, rotate within ±90° per axis unless clearly needed.
Do not use menu_item, add_model, delete_selection, scale, slice, or save actions.)";

bool is_model_load_allowed_action(const nlohmann::json& action)
{
    if (!action.is_object() || !action.contains("type") || !action["type"].is_string())
        return false;
    const std::string type = action["type"].get<std::string>();
    return type == "set_config" || type == "translate" || type == "rotate" ||
           type == "clone_selection" || type == "arrange";
}

std::shared_ptr<std::atomic<bool>> s_alive = std::make_shared<std::atomic<bool>>(true);

std::string build_model_load_context(Plater* plater)
{
    nlohmann::json ctx = nlohmann::json::parse(OllamaActionExecutor::build_context_json());

    nlohmann::json objects = nlohmann::json::array();
    if (plater) {
        for (const ModelObject* obj : plater->model().objects) {
            if (!obj)
                continue;
            nlohmann::json o;
            o["name"] = obj->name;
            const BoundingBoxf3 bb = obj->bounding_box_exact();
            o["size_mm"] = {
                {"x", bb.size().x()},
                {"y", bb.size().y()},
                {"z", bb.size().z()},
            };
            objects.push_back(std::move(o));
        }
    }
    ctx["loaded_objects"] = objects;
    ctx["object_count"]   = objects.size();
    ctx["trigger"]        = "model_loaded";
    return ctx.dump(2);
}

void push_plater_notification(Plater* plater, const std::string& text,
                              NotificationManager::NotificationLevel level =
                                  NotificationManager::NotificationLevel::RegularNotificationLevel)
{
    if (!plater)
        return;
    if (NotificationManager* nm = plater->get_notification_manager())
        nm->push_notification(NotificationType::CustomNotification, level, text);
}

void run_model_load_chat(Plater* plater)
{
    if (!s_alive->load() || wxGetApp().is_closing() || !plater)
        return;
    if (plater->model().objects.empty())
        return;

    const std::string context = build_model_load_context(plater);
    const std::string user_msg =
        std::string("Current slicer context (JSON):\n") + context + "\n\nUser request:\n" + kModelLoadUserPrompt;

    std::vector<OllamaMessage> messages;
    messages.push_back({"system", OllamaActionExecutor::build_system_prompt()});
    messages.push_back({"user", user_msg});

    OllamaProcessingNotice::show(plater, format(_L("AI is analyzing the model…")));

    const auto alive = s_alive;
    OllamaClient client(kDefaultHost);
    client.chat(kDefaultModel, messages, [alive, plater](const std::string& text, const std::string& error) {
        wxGetApp().CallAfter([alive, plater, text, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            Plater* p = wxGetApp().plater();
            if (!p)
                p = plater;
            if (!p)
                return;

            OllamaProcessingNotice::hide(p);

            if (!error.empty()) {
                push_plater_notification(p, std::string("AI load settings: ") + error,
                                         NotificationManager::NotificationLevel::WarningNotificationLevel);
                return;
            }

            try {
                nlohmann::json root = OllamaActionExecutor::extract_action_json(text);
                OllamaActionValidator::sanitize(root, kModelLoadUserPrompt);

                if (root.contains("actions") && root["actions"].is_array()) {
                    nlohmann::json filtered = nlohmann::json::array();
                    for (const auto& a : root["actions"]) {
                        if (is_model_load_allowed_action(a))
                            filtered.push_back(a);
                    }
                    root["actions"] = filtered;
                }

                const OllamaWorkflowRun workflow =
                    OllamaActionWorkflow::confirm_and_execute(root, p);

                if (workflow.cancelled) {
                    push_plater_notification(p,
                        format(_L("AI recommendations dismissed — no changes applied.")));
                    return;
                }
                if (workflow.preview_only) {
                    push_plater_notification(p,
                        format(_L("AI recommendations — preview only. Apply from Smart Print if needed.")));
                    return;
                }

                std::string summary;
                if (root.contains("message") && root["message"].is_string())
                    summary = root["message"].get<std::string>();
                if (summary.empty())
                    summary = "Applied AI recommendations for the loaded model.";

                if (!workflow.results.empty()) {
                    summary += " [";
                    for (size_t i = 0; i < workflow.results.size(); ++i) {
                        if (i)
                            summary += "; ";
                        summary += workflow.results[i].message;
                    }
                    summary += "]";
                }

                push_plater_notification(p, summary);
            } catch (const std::exception& ex) {
                push_plater_notification(p, std::string("AI load settings failed: ") + ex.what(),
                                         NotificationManager::NotificationLevel::WarningNotificationLevel);
            }
        });
    });
}

void ensure_ollama_then_run(Plater* plater)
{
    const auto alive = s_alive;
    OllamaClient client(kDefaultHost);
    client.list_models([alive, plater](const std::vector<std::string>&, const std::string& error) {
        wxGetApp().CallAfter([alive, plater, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            Plater* p = wxGetApp().plater();
            if (!p)
                p = plater;
            if (!p)
                return;

            if (error.empty()) {
                run_model_load_chat(p);
                return;
            }

            const wxString cmd = OllamaServerManager::resolve_ollama_command();
            const long pid     = wxExecute(cmd + " serve", wxEXEC_ASYNC);
            OllamaServerManager::mark_started(pid);

            OllamaProcessingNotice::show(p, format(_L("Starting Ollama for AI recommendations…")));

            auto* timer = new wxTimer(p);
            timer->Bind(wxEVT_TIMER, [timer, alive, p](wxTimerEvent&) {
                timer->Stop();
                delete timer;
                if (!alive->load() || wxGetApp().is_closing())
                    return;
                run_model_load_chat(p);
            });
            timer->StartOnce(1500);
        });
    });
}

} // namespace

void OllamaModelLoadAdvisor::schedule_after_model_load(Plater* plater)
{
    if (!plater || wxGetApp().is_closing())
        return;
    if (plater->model().objects.empty())
        return;

    const auto alive = s_alive;
    wxGetApp().CallAfter([alive, plater]() {
        if (!alive->load() || wxGetApp().is_closing())
            return;
        Plater* p = wxGetApp().plater();
        if (!p)
            p = plater;
        if (!p || p->model().objects.empty())
            return;

        OllamaProcessingNotice::show(p, format(_L("AI is analyzing the model…")));
        ensure_ollama_then_run(p);
    });
}

}} // namespace
