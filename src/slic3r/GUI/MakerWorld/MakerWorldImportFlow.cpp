#include "MakerWorldImportFlow.hpp"
#include "MakerWorldPickDialog.hpp"
#include "MakerWorldSearchService.hpp"
#include "MakerWorldTelemetry.hpp"
#include "MakerWorldUrl.hpp"

#include "../AICoach/AIGuiOrchestrator.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"

#include "slic3r/Utils/ICloudServiceAgent.hpp"

#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/weakref.h>

#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

#include <mutex>

namespace Slic3r { namespace GUI {

namespace {

struct PendingImportUi
{
    std::mutex                  mutex;
    bool                        in_progress{false};
    MakerWorldFlowUiCallbacks   callbacks;
};

PendingImportUi& pending_import_ui()
{
    static PendingImportUi state;
    return state;
}

bool stash_import_callbacks(MakerWorldFlowUiCallbacks callbacks)
{
    std::lock_guard<std::mutex> lock(pending_import_ui().mutex);
    if (pending_import_ui().in_progress)
        return false;
    pending_import_ui().in_progress = true;
    pending_import_ui().callbacks     = std::move(callbacks);
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
    {
        std::lock_guard<std::mutex> lock(pending_import_ui().mutex);
        cb = std::move(pending_import_ui().callbacks);
        pending_import_ui().callbacks   = {};
        pending_import_ui().in_progress = false;
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

    if (!stash_import_callbacks(callbacks)) {
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

        if (!stash_import_callbacks(callbacks)) {
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
    if (MakerWorldSearchService::user_wants_makerworld_import(user_text)) {
        run_import_from_user_text(parent, user_text, apply_mode, std::move(callbacks));
        return;
    }
    const std::string q = MakerWorldSearchService::normalize_search_query(user_text);
    run_search_from_query(parent, q.empty() ? user_text : q, apply_mode, std::move(callbacks));
}

}} // namespace
