#include "OllamaModelLoadAdvisor.hpp"

#include "OllamaActionExecutor.hpp"
#include "OllamaActionPipeline.hpp"
#include "OllamaActionRegistry.hpp"
#include "OllamaActionWorkflow.hpp"
#include "OllamaConfig.hpp"
#include "OllamaProcessingNotice.hpp"
#include "OllamaClient.hpp"
#include "OllamaServerManager.hpp"
#include "OllamaSettingRegistry.hpp"
#include "OllamaTelemetry.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../NotificationManager.hpp"
#include "../Plater.hpp"
#include "../BambuSmartPrint/PrintPlannerGui.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <wx/timer.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kModelLoadUserPrompt = R"(A 3D model was just loaded. The user is likely a beginner.

Review selection_size_mm, print_options, engineering_hints, and plain_language_hints in context.
Think outcome-first: what print failure should we prevent (adhesion, overhang, strength)?

Reply with JSON only (same rules as the system prompt).

- "message": 1–2 friendly sentences in plain language (no raw config key names).
- Minimum change: at most 1–2 settings in one set_config.
- Overhangs → support; small base → brim; tall narrow → lay flat with rotate.
- Prefer one set_config; no slice (auto re-slice after settings).

Allowed: set_config (print), translate, rotate, clone_selection, arrange.
Forbidden: menu_item, add_model, delete_selection, scale, slice, save_project.
Use only allowed_config_keys. Conservative transforms: ±50 mm, ±90° per axis.)";

struct AdvisorGate
{
    std::mutex                            mutex;
    bool                                  in_progress{false};
    size_t                                last_object_count{0};
    size_t                                last_model_fingerprint{0};
    std::chrono::steady_clock::time_point last_run{};
};

AdvisorGate& advisor_gate()
{
    static AdvisorGate gate;
    return gate;
}

size_t model_load_fingerprint(Plater* plater)
{
    if (!plater)
        return 0;
    size_t h = 0;
    for (const ModelObject* obj : plater->model().objects) {
        if (!obj)
            continue;
        h ^= std::hash<std::string>{}(obj->name) + 0x9e3779b9 + (h << 6) + (h >> 2);
        if (!obj->input_file.empty())
            h ^= std::hash<std::string>{}(obj->input_file) + 0x9e3779b9 + (h << 6) + (h >> 2);
        for (const ModelVolume* vol : obj->volumes) {
            if (vol && !vol->source.input_file.empty())
                h ^= std::hash<std::string>{}(vol->source.input_file) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
    }
    return h;
}

bool try_begin_schedule(Plater* plater)
{
    if (!plater)
        return false;
    std::lock_guard<std::mutex> lock(advisor_gate().mutex);
    const size_t                count = plater->model().objects.size();
    const size_t                fp    = model_load_fingerprint(plater);
    const auto                  now   = std::chrono::steady_clock::now();
    if (advisor_gate().in_progress) {
        OllamaTelemetry::advisor_dedup_skipped();
        return false;
    }
    if (count == advisor_gate().last_object_count && fp == advisor_gate().last_model_fingerprint
        && advisor_gate().last_run != std::chrono::steady_clock::time_point{}
        && now - advisor_gate().last_run < std::chrono::seconds(3)) {
        OllamaTelemetry::advisor_dedup_skipped();
        return false;
    }
    advisor_gate().in_progress            = true;
    advisor_gate().last_object_count      = count;
    advisor_gate().last_model_fingerprint = fp;
    advisor_gate().last_run               = now;
    OllamaTelemetry::advisor_scheduled();
    return true;
}

void finish_schedule()
{
    std::lock_guard<std::mutex> lock(advisor_gate().mutex);
    advisor_gate().in_progress = false;
}

std::shared_ptr<std::atomic<bool>> s_alive = std::make_shared<std::atomic<bool>>(true);

std::string build_model_load_context(Plater* plater)
{
    nlohmann::json ctx = nlohmann::json::parse(OllamaActionExecutor::build_compact_context_json());
    if (auto* bundle = wxGetApp().preset_bundle) {
        const bool ko = GUI::wxGetApp().current_language_code().StartsWith("ko");
        ctx["setting_catalog"] =
            OllamaSettingRegistry::build_priority_catalog(&bundle->prints.get_edited_preset().config, ko, 8);
    }

    nlohmann::json objects = nlohmann::json::array();
    if (plater) {
        for (const ModelObject* obj : plater->model().objects) {
            if (!obj)
                continue;
            nlohmann::json o;
            o["name"] = obj->name;
            const BoundingBoxf3 bb = obj->bounding_box_exact();
            o["size_mm"]           = {
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
    if (plater->model().objects.empty()) {
        finish_schedule();
        return;
    }

    const std::string context = build_model_load_context(plater);
    const std::string user_msg =
        std::string("Current slicer context (JSON):\n") + context + "\n\nUser request:\n" + kModelLoadUserPrompt;

    std::vector<OllamaMessage> messages;
    messages.push_back({"system", OllamaActionExecutor::build_system_prompt()});
    messages.push_back({"user", user_msg});

    OllamaProcessingNotice::show(plater, _u8L("AI is analyzing the model…"));

    const auto alive = s_alive;
    OllamaClient client(ollama_host_from_config());
    client.chat(
        ollama_model_from_config(), messages,
        [alive, plater](const std::string& text, const std::string& error) {
            wxGetApp().CallAfter([alive, plater, text, error]() {
                if (!alive->load() || wxGetApp().is_closing()) {
                    finish_schedule();
                    return;
                }
                Plater* p = wxGetApp().plater();
                if (!p)
                    p = plater;
                if (!p) {
                    finish_schedule();
                    return;
                }

                OllamaProcessingNotice::hide(p);

                if (!error.empty()) {
                    push_plater_notification(p, std::string("AI load settings: ") + error,
                                             NotificationManager::NotificationLevel::WarningNotificationLevel);
                    finish_schedule();
                    return;
                }

                try {
                    nlohmann::json root = OllamaActionPipeline::extract_from_assistant_text(text);
                    const BambuSmartPrint::PrintPlan merged =
                        PrintPlannerGui::plan_from_assistant(p, kModelLoadUserPrompt, root);
                    root = merged.root;
                    PrintPlannerGui::apply_plan_to_service(merged);
                    OllamaPipelineOptions opt;
                    opt.apply_mode         = true;
                    opt.include_makerworld = false;
                    opt.advisor_filter     = true;
                    opt.user_request       = kModelLoadUserPrompt;
                    OllamaActionPipeline::process_actions(root, opt);

                    const OllamaWorkflowRun workflow = OllamaActionWorkflow::confirm_and_execute(root, p);

                    if (workflow.cancelled) {
                        push_plater_notification(p, _u8L("AI recommendations dismissed — no changes applied."));
                        finish_schedule();
                        return;
                    }
                    if (workflow.preview_only) {
                        push_plater_notification(p,
                                                 _u8L("AI recommendations — preview only. Apply from Smart Print if needed."));
                        finish_schedule();
                        return;
                    }

                    std::string summary;
                    if (root.contains("message") && root["message"].is_string())
                        summary = root["message"].get<std::string>();

                    bool effective = false;
                    for (const auto& r : workflow.results) {
                        if (r.success && r.effective_change) {
                            effective = true;
                            break;
                        }
                    }
                    if (!effective) {
                        push_plater_notification(p, _u8L("AI recommendations — no settings were changed."));
                        finish_schedule();
                        return;
                    }

                    if (summary.empty())
                        summary = _u8L("Applied AI recommendations for the loaded model.");

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
                finish_schedule();
            });
        },
        OllamaRequestKind::Advisor);
}

void ensure_ollama_then_run(Plater* plater)
{
    const auto alive = s_alive;
    OllamaClient client(ollama_host_from_config());
    client.list_models([alive, plater](const std::vector<std::string>&, const std::string& error) {
        wxGetApp().CallAfter([alive, plater, error]() {
            if (!alive->load() || wxGetApp().is_closing()) {
                finish_schedule();
                return;
            }
            Plater* p = wxGetApp().plater();
            if (!p)
                p = plater;
            if (!p) {
                finish_schedule();
                return;
            }

            if (error.empty()) {
                run_model_load_chat(p);
                return;
            }

            const wxString cmd = OllamaServerManager::resolve_ollama_command();
            OllamaServerManager::note_serve_spawn_attempt();
            OllamaTelemetry::server_spawn_attempt(OllamaServerManager::serve_spawn_attempt_count(),
                                                    cmd.utf8_string());
            const long     pid = wxExecute(cmd + " serve", wxEXEC_ASYNC);
            if (pid > 0) {
                OllamaServerManager::mark_started(pid);
                OllamaTelemetry::server_spawn_success(pid);
            } else {
                OllamaTelemetry::server_spawn_failed("wxExecute failed");
            }

            OllamaProcessingNotice::show(p, _u8L("Starting Ollama for AI recommendations…"));

            auto* timer = new wxTimer(p);
            timer->Bind(wxEVT_TIMER, [timer, alive, p](wxTimerEvent&) {
                timer->Stop();
                delete timer;
                if (!alive->load() || wxGetApp().is_closing()) {
                    finish_schedule();
                    return;
                }
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
        if (!try_begin_schedule(p))
            return;

        OllamaProcessingNotice::show(p, _u8L("AI is analyzing the model…"));
        ensure_ollama_then_run(p);
    });
}

}} // namespace
