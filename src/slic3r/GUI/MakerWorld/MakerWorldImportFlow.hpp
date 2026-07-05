#ifndef slic3r_MakerWorldImportFlow_hpp_
#define slic3r_MakerWorldImportFlow_hpp_

#include "MakerWorldTypes.hpp"

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <string>
#include <vector>

class wxString;
class wxWindow;

namespace Slic3r { namespace GUI {

struct MakerWorldFlowUiCallbacks
{
    std::function<void(const wxString& assistant_message)> append_chat;
    std::function<void(bool busy, const wxString& status)> set_busy;
    std::function<void()>                                  on_flow_finished;
};

/** Runs search / pick / confirm / request_model_download. All UI on main thread. */
class MakerWorldImportFlow
{
public:
    static void run_search_from_query(wxWindow* parent, const std::string& query, bool apply_mode,
                                      MakerWorldFlowUiCallbacks callbacks = {});

    static void run_import_from_user_text(wxWindow* parent, const std::string& user_text, bool apply_mode,
                                         MakerWorldFlowUiCallbacks callbacks = {});

    /** Search or import depending on user text (URL/import intent vs keyword search). */
    static void run_user_makerworld_request(wxWindow* parent, const std::string& user_text, bool apply_mode,
                                            MakerWorldFlowUiCallbacks callbacks = {});

    /** Search top 3, show numbered picks + countdown, then import, slice, and send to printer. */
    static void run_search_and_offer_print(wxWindow* parent, const std::string& query, bool apply_mode,
                                           MakerWorldFlowUiCallbacks callbacks = {});

    static bool confirm_and_import(wxWindow* parent, const MakerWorldCandidate& candidate, bool apply_mode,
                                   MakerWorldFlowUiCallbacks callbacks = {});

    /** Dispatch a single MakerWorld agent action (makerworld_search / makerworld_find_and_print /
     *  import_makerworld). Returns true when the action was a MakerWorld action and was handled.
     *  Shared by the single-shot chat path and the agent-loop executor so routing lives in one place. */
    static bool run_agent_action(const nlohmann::json& action, wxWindow* parent, bool apply_mode,
                                 const std::string& user_req, MakerWorldFlowUiCallbacks callbacks = {});

    static void import_download_info(const std::string& download_info, const std::string& design_id);

    /** Plater calls when import_model_id finishes (success or failure). */
    static void notify_plater_import_done(bool ok, const std::string& detail = {});
};

}} // namespace

#endif
