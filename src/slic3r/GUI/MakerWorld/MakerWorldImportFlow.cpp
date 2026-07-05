#include "MakerWorldImportFlow.hpp"
#include "MakerWorldIntent.hpp"
#include "MakerWorldPickDialog.hpp"
#include "MakerWorldPrintOfferDialog.hpp"
#include "MakerWorldSearchService.hpp"
#include "MakerWorldTelemetry.hpp"
#include "MakerWorldUrl.hpp"

#include "../AICoach/AIGuiOrchestrator.hpp"
#include "../GUI_App.hpp"
#include "../GLToolbar.hpp"
#include "../I18N.hpp"
#include "../OllamaAssistant/OllamaAgentEventBus.hpp"
#include "../OllamaAssistant/OllamaUserFlow.hpp"
#include "../Plater.hpp"

#include "slic3r/Utils/ICloudServiceAgent.hpp"

#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/weakref.h>

#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

#include <atomic>
#include <mutex>

namespace Slic3r { namespace GUI {

namespace {

struct PendingImportUi
{
    std::mutex                  mutex;
    bool                        in_progress{false};
    bool                        slice_and_send_after_import{false};
    MakerWorldFlowUiCallbacks   callbacks;
};

PendingImportUi& pending_import_ui()
{
    static PendingImportUi state;
    return state;
}

bool stash_import_callbacks(MakerWorldFlowUiCallbacks callbacks, bool slice_and_send)
{
    std::lock_guard<std::mutex> lock(pending_import_ui().mutex);
    if (pending_import_ui().in_progress)
        return false;
    pending_import_ui().in_progress                  = true;
    pending_import_ui().slice_and_send_after_import  = slice_and_send;
    pending_import_ui().callbacks                    = std::move(callbacks);
    return true;
}

std::string extract_first_url(const std::string& text)
{
    static const boost::regex link_re(R"((https?://[^\s]+))", boost::regex_constants::icase);
    boost::smatch m;
    if (boost::regex_search(text, m, link_re))
        return m[1].str();
    return {};
}

bool is_direct_model_url(const std::string& url)
{
    if (url.empty())
        return false;
    std::string lower = url;
    boost::algorithm::to_lower(lower);
    return lower.find(".3mf") != std::string::npos || lower.find("/download") != std::string::npos;
}

void chat_notify(const MakerWorldFlowUiCallbacks& cb, const wxString& msg)
{
    if (cb.append_chat)
        cb.append_chat(msg);
}

void set_flow_busy(const MakerWorldFlowUiCallbacks& cb, bool busy, const wxString& status = {})
{
    if (cb.set_busy)
        cb.set_busy(busy, status);
}

void finish_flow(const MakerWorldFlowUiCallbacks& cb)
{
    AIGuiOrchestrator::instance().on_makerworld_search_end();
    set_flow_busy(cb, false);
    if (cb.on_flow_finished)
        cb.on_flow_finished();
}

/** Slice the current plate, then send to printer once slicing completes.
 *  Subscribes for a single SliceDone event, unsubscribes after handling it, and
 *  calls finish_flow() only after slice+send resolves (success OR failure). */
void schedule_slice_and_send(MakerWorldFlowUiCallbacks cb)
{
    Plater* plater = wxGetApp().plater();
    if (!plater) {
        chat_notify(cb, _L("The slicer is not ready to slice. Try again."));
        finish_flow(cb);
        return;
    }

    auto done   = std::make_shared<std::atomic<bool>>(false);
    auto sub_id = std::make_shared<OllamaSubscriptionId>(0);

    *sub_id = OllamaAgentEventBus::instance().subscribe([done, sub_id, cb](const OllamaAgentEvent& evt) {
        if (evt.kind != OllamaAgentEventKind::SliceDone)
            return;
        // One-shot: guard against double-fire, then detach from the bus so this
        // handler never runs for unrelated future slices.
        if (done->exchange(true))
            return;
        OllamaAgentEventBus::instance().unsubscribe(*sub_id);

        const bool ok = evt.payload.value("success", false);
        wxGetApp().CallAfter([ok, cb]() {
            Plater* p = wxGetApp().plater();
            if (!p) {
                chat_notify(cb, _L("The slicer is no longer available."));
                finish_flow(cb);
                return;
            }
            if (!ok) {
                chat_notify(cb, _L("Slicing failed. Adjust the model or settings and try again."));
                finish_flow(cb);
                return;
            }

            const OllamaFlowDispatchResult dispatch = OllamaUserFlow::dispatch_coach_action("send_print", p);
            if (dispatch.setup_blocked) {
                chat_notify(cb, dispatch.blocked_message.empty()
                                    ? _L("Complete printer setup before sending to the printer.")
                                    : wxString::FromUTF8(dispatch.blocked_message));
            } else {
                chat_notify(cb, _L("Slicing finished — sending the model to your printer."));
            }
            finish_flow(cb);
        });
    });

    wxPostEvent(plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));
}

bool import_candidate_direct(wxWindow* parent, const MakerWorldCandidate& candidate, bool apply_mode,
                             MakerWorldFlowUiCallbacks callbacks, bool slice_and_send_after)
{
    if (!apply_mode) {
        chat_notify(callbacks,
            wxString::Format(_L("Selected: %s (id %s). Switch to Apply mode to import."),
                wxString::FromUTF8(candidate.title), wxString::FromUTF8(candidate.design_id)));
        finish_flow(callbacks);
        return false;
    }

    if (candidate.design_id.empty() && candidate.download_url.empty()) {
        chat_notify(callbacks, _L("Invalid model selection."));
        finish_flow(callbacks);
        return false;
    }

    set_flow_busy(callbacks, true, _L("Resolving download…"));

    const MakerWorldImportPayload payload = MakerWorldSearchService::resolve_import(candidate);
    if (!payload.ok) {
        MakerWorldTelemetry::import_finished(false, candidate.design_id, payload.error);
        chat_notify(callbacks, wxString::FromUTF8(payload.error));
        finish_flow(callbacks);
        return false;
    }

    if (!stash_import_callbacks(callbacks, slice_and_send_after)) {
        chat_notify(callbacks, _L("Another MakerWorld import is already in progress."));
        finish_flow(callbacks);
        return false;
    }

    set_flow_busy(callbacks, true, _L("Downloading model…"));
    MakerWorldImportFlow::import_download_info(payload.download_info, candidate.design_id);
    chat_notify(callbacks,
        wxString::Format(_L("Downloading \"%s\" from MakerWorld…"),
            wxString::FromUTF8(candidate.title.empty() ? candidate.design_id : candidate.title)));
    return true;
}

wxWindow* safe_parent(wxWindow* parent)
{
    return parent ? parent : wxGetApp().GetTopWindow();
}

} // namespace

void MakerWorldImportFlow::import_download_info(const std::string& download_info, const std::string& design_id)
{
    MakerWorldTelemetry::import_started(design_id);
    wxGetApp().request_model_download(wxString::FromUTF8(download_info));
}

void MakerWorldImportFlow::notify_plater_import_done(bool ok, const std::string& detail)
{
    MakerWorldFlowUiCallbacks cb;
    bool slice_and_send = false;
    {
        std::lock_guard<std::mutex> lock(pending_import_ui().mutex);
        cb                  = std::move(pending_import_ui().callbacks);
        slice_and_send      = pending_import_ui().slice_and_send_after_import;
        pending_import_ui().callbacks                      = {};
        pending_import_ui().in_progress                    = false;
        pending_import_ui().slice_and_send_after_import    = false;
    }
    MakerWorldTelemetry::plater_import_done(ok, detail);
    if (cb.append_chat) {
        if (ok)
            cb.append_chat(_L("Model loaded from MakerWorld."));
        else if (!detail.empty())
            cb.append_chat(wxString::Format(_L("Import failed: %s"), wxString::FromUTF8(detail)));
        else
            cb.append_chat(_L("Import failed."));
    }
    if (ok && slice_and_send) {
        if (cb.append_chat)
            cb.append_chat(_L("Slicing and sending to printer…"));
        // finish_flow is deferred until slice+send completes (success or failure)
        // so busy state / on_flow_finished do not fire mid-slice.
        schedule_slice_and_send(cb);
        return;
    }
    finish_flow(cb);
}

bool MakerWorldImportFlow::confirm_and_import(wxWindow* parent, const MakerWorldCandidate& candidate,
                                              bool apply_mode, MakerWorldFlowUiCallbacks callbacks)
{
    if (!apply_mode) {
        chat_notify(callbacks,
            wxString::Format(_L("Selected: %s (id %s). Switch to Apply mode to import."),
                wxString::FromUTF8(candidate.title), wxString::FromUTF8(candidate.design_id)));
        finish_flow(callbacks);
        return false;
    }

    if (candidate.design_id.empty() && candidate.download_url.empty()) {
        chat_notify(callbacks, _L("Invalid model selection."));
        finish_flow(callbacks);
        return false;
    }

    const wxString msg = wxString::Format(
        _L("Import \"%s\" from MakerWorld into the slicer?"),
        wxString::FromUTF8(candidate.title.empty() ? candidate.design_id : candidate.title));

    if (wxMessageBox(msg, _L("MakerWorld"), wxYES_NO | wxICON_QUESTION, safe_parent(parent)) != wxYES) {
        chat_notify(callbacks, _L("Import cancelled."));
        finish_flow(callbacks);
        return false;
    }

    set_flow_busy(callbacks, true, _L("Resolving download…"));

    const MakerWorldImportPayload payload = MakerWorldSearchService::resolve_import(candidate);
    if (!payload.ok) {
        MakerWorldTelemetry::import_finished(false, candidate.design_id, payload.error);
        chat_notify(callbacks, wxString::FromUTF8(payload.error));
        const bool needs_login = payload.error.find("Sign in") != std::string::npos
            || payload.error.find("Sign out and sign in") != std::string::npos
            || payload.error.find("session expired") != std::string::npos
            || payload.error.find("로그인") != std::string::npos;
        if (needs_login) {
            if (wxMessageBox(_L("Bambu Cloud login is required to download this model. Open login now?"),
                             _L("MakerWorld"), wxYES_NO | wxICON_INFORMATION, safe_parent(parent))
                == wxYES)
                wxGetApp().request_login(true, BBL_CLOUD_PROVIDER);
        } else if (!payload.detail_page_url.empty()) {
            const std::string page = absolute_makerworld_browser_url(payload.detail_page_url);
            if (wxMessageBox(_L("Open model page in browser?"), _L("MakerWorld"), wxYES_NO | wxICON_WARNING,
                             safe_parent(parent))
                == wxYES)
                wxLaunchDefaultBrowser(wxString::FromUTF8(page));
        }
        finish_flow(callbacks);
        return false;
    }

    if (!stash_import_callbacks(callbacks, false)) {
        chat_notify(callbacks, _L("Another MakerWorld import is already in progress."));
        finish_flow(callbacks);
        return false;
    }

    set_flow_busy(callbacks, true, _L("Downloading model…"));
    import_download_info(payload.download_info, candidate.design_id);
    chat_notify(callbacks,
        wxString::Format(_L("Downloading \"%s\" from MakerWorld…"),
            wxString::FromUTF8(candidate.title.empty() ? candidate.design_id : candidate.title)));
    return true;
}

void MakerWorldImportFlow::run_search_from_query(wxWindow* parent, const std::string& query, bool apply_mode,
                                                 MakerWorldFlowUiCallbacks callbacks)
{
    if (!apply_mode) {
        chat_notify(callbacks,
            _L("Question mode: you can search and pick a model. Switch to Apply mode to import into the plate."));
    }

    AIGuiOrchestrator::instance().on_makerworld_search_begin();
    MakerWorldSearchService::prefetch_staffpick_pool();
    set_flow_busy(callbacks, true, _L("Searching MakerWorld…"));

    if (!wxGetApp().app_config) {
        chat_notify(callbacks, _L("Application configuration is not ready. Restart the slicer and try again."));
        finish_flow(callbacks);
        return;
    }

    wxWeakRef<wxWindow> weak_parent(parent);

    MakerWorldSearchService::search_async(query, MakerWorldSearchService::build_context(),
        [weak_parent, apply_mode, query, callbacks](MakerWorldSearchResult result) {
        wxWindow* p = weak_parent.get();
        if (!p && !wxGetApp().is_closing())
            p = wxGetApp().GetTopWindow();

        if (!result.ok) {
            chat_notify(callbacks, wxString::FromUTF8(result.error));
            wxString err = wxString::FromUTF8(result.error);
            err += "\n\n" + _L("Open MakerWorld search in browser?");
            if (p && wxMessageBox(err, _L("MakerWorld"), wxYES_NO | wxICON_INFORMATION, p) == wxYES) {
                const std::string page = MakerWorldSearchService::makerworld_search_page_url(query);
                if (!page.empty())
                    wxLaunchDefaultBrowser(wxString::FromUTF8(page));
            }
            finish_flow(callbacks);
            return;
        }

        wxString source_note;
        if (result.source == "staffpick_filter")
            source_note = _L(" (from featured list)");

        chat_notify(callbacks,
            wxString::Format(_L("Found %d models on MakerWorld%s. Choose one in the dialog."),
                static_cast<int>(result.candidates.size()), source_note));

        if (!p) {
            finish_flow(callbacks);
            return;
        }

        MakerWorldPickDialog dlg(p, result.candidates);
        if (dlg.ShowModal() != wxID_OK || !dlg.has_selection()) {
            chat_notify(callbacks, _L("Search cancelled."));
            finish_flow(callbacks);
            return;
        }

        confirm_and_import(p, dlg.selected_candidate(), apply_mode, callbacks);
    });
}

void MakerWorldImportFlow::run_import_from_user_text(wxWindow* parent, const std::string& user_text, bool apply_mode,
                                                   MakerWorldFlowUiCallbacks callbacks)
{
    const std::string url = extract_first_url(user_text);
    if (!url.empty() && (is_makerworld_host_url(url) || is_direct_model_url(url))) {
        if (!apply_mode) {
            chat_notify(callbacks, _L("Switch to Apply mode to import a MakerWorld model."));
            return;
        }

        AIGuiOrchestrator::instance().on_makerworld_search_begin();
        set_flow_busy(callbacks, true, _L("Importing from MakerWorld…"));
        chat_notify(callbacks, _L("Preparing MakerWorld import…"));

        const MakerWorldImportPayload payload = MakerWorldSearchService::resolve_import_from_url(url);
        if (!payload.ok) {
            chat_notify(callbacks, wxString::FromUTF8(payload.error));
            finish_flow(callbacks);
            return;
        }

        const std::string id = parse_design_id_from_url(url);
        if (wxMessageBox(_L("Import this MakerWorld model into the slicer?"), _L("MakerWorld"), wxYES_NO | wxICON_QUESTION,
                         safe_parent(parent))
            != wxYES) {
            chat_notify(callbacks, _L("Import cancelled."));
            finish_flow(callbacks);
            return;
        }

        if (!stash_import_callbacks(callbacks, false)) {
            chat_notify(callbacks, _L("Another MakerWorld import is already in progress."));
            finish_flow(callbacks);
            return;
        }

        import_download_info(payload.download_info, id);
        chat_notify(callbacks, _L("Downloading model from MakerWorld…"));
        return;
    }

    const std::string q = MakerWorldSearchService::normalize_search_query(user_text);
    run_search_from_query(parent, q.empty() ? user_text : q, apply_mode, callbacks);
}

void MakerWorldImportFlow::run_user_makerworld_request(wxWindow* parent, const std::string& user_text, bool apply_mode,
                                                      MakerWorldFlowUiCallbacks callbacks)
{
    if (MakerWorldIntent::user_wants_makerworld_import(user_text)) {
        run_import_from_user_text(parent, user_text, apply_mode, std::move(callbacks));
        return;
    }
    const std::string q = MakerWorldSearchService::normalize_search_query(user_text);
    run_search_from_query(parent, q.empty() ? user_text : q, apply_mode, std::move(callbacks));
}

void MakerWorldImportFlow::run_search_and_offer_print(wxWindow* parent, const std::string& query, bool apply_mode,
                                                      MakerWorldFlowUiCallbacks callbacks)
{
    if (!apply_mode) {
        chat_notify(callbacks, _L("Switch to Apply mode to search MakerWorld and print."));
        return;
    }

    AIGuiOrchestrator::instance().on_makerworld_search_begin();
    MakerWorldSearchService::prefetch_staffpick_pool();
    set_flow_busy(callbacks, true, _L("Searching MakerWorld…"));

    if (!wxGetApp().app_config) {
        chat_notify(callbacks, _L("Application configuration is not ready. Restart the slicer and try again."));
        finish_flow(callbacks);
        return;
    }

    const std::string search_q = query.empty() ? std::string{} : MakerWorldSearchService::normalize_search_query(query);
    wxWeakRef<wxWindow> weak_parent(parent);

    MakerWorldSearchService::search_async(search_q.empty() ? query : search_q, MakerWorldSearchService::build_context(),
        [weak_parent, apply_mode, callbacks](MakerWorldSearchResult result) {
        wxWindow* p = weak_parent.get();
        if (!p && !wxGetApp().is_closing())
            p = wxGetApp().GetTopWindow();

        if (!result.ok || result.candidates.empty()) {
            if (!result.ok)
                chat_notify(callbacks, wxString::FromUTF8(result.error));
            else
                chat_notify(callbacks, _L("No models found on MakerWorld."));
            finish_flow(callbacks);
            return;
        }

        std::vector<MakerWorldCandidate> top;
        top.reserve(3);
        for (size_t i = 0; i < result.candidates.size() && i < 3; ++i)
            top.push_back(result.candidates[i]);

        wxString chat_lines;
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        for (size_t i = 0; i < top.size(); ++i) {
            const auto& c = top[i];
            chat_lines += wxString::Format("%zu. %s", i + 1, wxString::FromUTF8(c.title));
            if (!c.author.empty())
                chat_lines += wxString::Format(" — %s", wxString::FromUTF8(c.author));
            chat_lines += "\n";
        }
        chat_notify(callbacks, ko ? wxString::FromUTF8("MakerWorld 검색 결과:\n") + chat_lines
                                 : _L("MakerWorld results:\n") + chat_lines);

        if (!p) {
            finish_flow(callbacks);
            return;
        }

        MakerWorldPrintOfferDialog dlg(p, top);
        if (dlg.ShowModal() != wxID_OK || !dlg.has_selection()) {
            chat_notify(callbacks, _L("Print cancelled."));
            finish_flow(callbacks);
            return;
        }

        import_candidate_direct(p, dlg.selected_candidate(), apply_mode, callbacks, true);
    });
}

bool MakerWorldImportFlow::run_agent_action(const nlohmann::json& action, wxWindow* parent, bool apply_mode,
                                            const std::string& user_req, MakerWorldFlowUiCallbacks callbacks)
{
    if (!action.is_object() || !action.contains("type") || !action["type"].is_string())
        return false;

    const std::string type = action["type"].get<std::string>();
    if (type == "makerworld_search") {
        std::string query = action.value("query", "");
        if (query.empty())
            query = MakerWorldSearchService::normalize_search_query(user_req);
        run_search_from_query(parent, query, apply_mode, std::move(callbacks));
        return true;
    }
    if (type == "makerworld_find_and_print") {
        std::string query = action.value("query", "");
        if (query.empty())
            query = MakerWorldSearchService::normalize_search_query(user_req);
        run_search_and_offer_print(parent, query, apply_mode, std::move(callbacks));
        return true;
    }
    if (type == "import_makerworld") {
        if (!apply_mode) {
            chat_notify(callbacks, _L("Switch to Apply mode to import a MakerWorld model."));
        } else if (action.contains("url") && action["url"].is_string()) {
            run_import_from_user_text(parent, action["url"].get<std::string>(), true, std::move(callbacks));
        } else {
            MakerWorldCandidate c;
            c.design_id = action.value("design_id", "");
            c.title     = action.value("title", "model_" + c.design_id);
            if (!c.design_id.empty())
                confirm_and_import(parent, c, true, std::move(callbacks));
            else
                chat_notify(callbacks, _L("Invalid model selection."));
        }
        return true;
    }
    return false;
}

}} // namespace
