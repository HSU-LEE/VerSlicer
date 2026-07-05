#include "PrintJobStepExecutors.hpp"

#include "../GUI_App.hpp"
#include "../GLToolbar.hpp"          // EVT_GLTOOLBAR_SLICE_PLATE, SimpleEvent
#include "../PartPlate.hpp"
#include "../Plater.hpp"

#include "../ModelSearch/CandidateRanker.hpp"
#include "../ModelSearch/ModelSearchService.hpp"

#include "../MakerWorld/MakerWorldPrintOfferDialog.hpp"
#include "../MakerWorld/MakerWorldTypes.hpp"

#include "../OllamaAssistant/AiLocale.hpp"

#include "../BambuSmartPrint/PrintPlannerGui.hpp"

#include "../OllamaAssistant/OllamaActionExecutor.hpp"
#include "../OllamaAssistant/OllamaConfigProposalBuilder.hpp"
#include "../OllamaAssistant/OllamaConfigProposalCache.hpp"
#include "../OllamaAssistant/OllamaMeshOps.hpp"
#include "../OllamaAssistant/OllamaUserFlow.hpp"

#include "libslic3r/BambuSmartPrint/GeometryAssessmentProducer.hpp"
#include "libslic3r/BambuSmartPrint/MeshHealthService.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <wx/dialog.h>
#include <wx/event.h>

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <thread>
#include <utility>

namespace Slic3r { namespace GUI { namespace AIPipeline {

using namespace Slic3r::BambuSmartPrint;

namespace {

// Cross-provider ModelCandidate -> legacy MakerWorldCandidate for the countdown
// offer dialog. Lossless w.r.t. the fields the dialog + import path consume.
MakerWorldCandidate to_makerworld_candidate(const ModelCandidate& c)
{
    MakerWorldCandidate mw;
    mw.design_id      = c.id;
    mw.model_id       = c.model_id;
    mw.profile_id     = c.profile_id;
    mw.title          = c.title;
    mw.author         = c.author;
    mw.cover_url      = c.thumbnail_url;
    mw.license        = c.license;
    mw.download_url   = c.download_url;
    mw.filename       = c.filename;
    mw.download_count = c.downloads;
    mw.login_required = c.login_required;
    return mw;
}

} // namespace

void PrintJobStepExecutors::execute_search_async(const std::string& user_text, ModelSearchResultCallback callback)
{
    // build_context() reads wx state and MUST run on the main thread (asserted by
    // the caller: orchestrator invokes this from the Searching step on main).
    const ModelSearchContext    ctx  = ModelSearchService::build_context();
    const CandidateRankerConfig rank = CandidateRankerConfig::from_app_config(wxGetApp().app_config);
    ModelSearchService::instance().search_all_providers(user_text, std::move(callback), ctx, rank);
}

int PrintJobStepExecutors::show_print_offer_dialog(wxWindow* parent, const std::vector<ModelCandidate>& candidates)
{
    if (candidates.empty())
        return -1;

    std::vector<MakerWorldCandidate> top;
    const size_t limit = std::min<size_t>(candidates.size(), 3);
    top.reserve(limit);
    for (size_t i = 0; i < limit; ++i)
        top.push_back(to_makerworld_candidate(candidates[i]));

    wxWindow* p = parent ? parent : wxGetApp().GetTopWindow();
    MakerWorldPrintOfferDialog dlg(p, top);
    if (dlg.ShowModal() != wxID_OK || !dlg.has_selection())
        return -1;

    const int idx = dlg.selected_index();
    if (idx < 0 || static_cast<size_t>(idx) >= limit)
        return -1;
    // The dialog was shown the prefix [0, limit), so its index is also the index
    // into the caller's full `candidates` vector.
    return idx;
}

PrintJobStepExecutors::ImportResult PrintJobStepExecutors::resolve_import_blocking(const ModelCandidate& candidate)
{
    ImportResult out;

    if (candidate.id.empty() && candidate.download_url.empty()) {
        out.error = AiLocale::text("Invalid model selection.", "잘못된 모델 선택입니다.").utf8_string();
        return out;
    }

    const ModelImportPayload payload = ModelSearchService::instance().resolve_import(candidate);
    out.detail_page_url = payload.detail_page_url;
    if (!payload.ok) {
        out.error = payload.error;
        out.needs_login = payload.error.find("Sign in") != std::string::npos
                       || payload.error.find("session expired") != std::string::npos
                       || payload.error.find("로그인") != std::string::npos;
        out.no_download_link = !out.needs_login
                            && (payload.error.find("download link") != std::string::npos
                                || payload.error.find("다운로드 링크") != std::string::npos);
        return out;
    }
    if (payload.download_info.empty()) {
        out.no_download_link = true;
        out.error = AiLocale::text("No download link was returned for this model.",
                                   "이 모델의 다운로드 링크를 받지 못했습니다.").utf8_string();
        return out;
    }

    out.download_info = payload.download_info;
    out.ok            = true;
    return out;
}

void PrintJobStepExecutors::resolve_import_async(const ModelCandidate& candidate,
                                                 std::function<void(ImportResult)> callback)
{
    // resolve_import performs sync HTTP (up to tens of seconds with retries), so
    // it must not run on the main thread. The candidate is copied into the
    // worker; the result is marshaled back via CallAfter. The caller re-validates
    // its own job/liveness guards inside `callback`.
    std::thread([candidate, cb = std::move(callback)]() {
        ImportResult res;
        try {
            res = resolve_import_blocking(candidate);
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(error) << "AIPipeline: resolve_import threw: " << ex.what();
            res.error = AiLocale::text("Could not resolve the model download.",
                                       "모델 다운로드 정보를 가져오지 못했습니다.").utf8_string();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "AIPipeline: resolve_import threw: unknown error";
            res.error = AiLocale::text("Could not resolve the model download.",
                                       "모델 다운로드 정보를 가져오지 못했습니다.").utf8_string();
        }
        if (wxGetApp().is_closing())
            return; // shutting down: never post to a dying main loop
        wxGetApp().CallAfter([cb, res]() {
            if (wxGetApp().is_closing())
                return;
            if (cb)
                cb(res);
        });
    }).detach();
}

void PrintJobStepExecutors::begin_download(const std::string& download_info)
{
    wxGetApp().request_model_download(wxString::FromUTF8(download_info));
}

PrintJobStepExecutors::MeshHealthResult PrintJobStepExecutors::assess_mesh_health()
{
    MeshHealthResult out;
    Plater* plater = wxGetApp().plater();
    if (!plater || !wxGetApp().preset_bundle)
        return out;

    const DynamicPrintConfig      cfg    = wxGetApp().preset_bundle->full_config();
    const MeshAssist::MeshAnalysisParams params = MeshHealthService::params_from_config(cfg);
    const MeshHealthPlateReport   report = MeshHealthService::analyze_objects(plater->model().objects, params);

    out.json         = MeshHealthService::to_json(report);
    out.needs_repair = report.any_needs_repair;
    out.valid        = true;
    return out;
}

int PrintJobStepExecutors::attempt_repair(const nlohmann::json& mesh_health)
{
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return 0;
    if (!mesh_health.is_object() || !mesh_health.contains("objects") || !mesh_health["objects"].is_array())
        return 0;

    int repaired = 0;
    const auto& objects = mesh_health["objects"];
    for (size_t obj_idx = 0; obj_idx < objects.size(); ++obj_idx) {
        const auto& oj = objects[obj_idx];
        if (!oj.is_object() || !oj.value("any_needs_repair", false))
            continue;
        if (!oj.contains("volumes") || !oj["volumes"].is_array())
            continue;
        const auto& volumes = oj["volumes"];
        for (size_t vol_idx = 0; vol_idx < volumes.size(); ++vol_idx) {
            const auto& vj = volumes[vol_idx];
            if (!vj.is_object() || !vj.value("needs_repair", false))
                continue;
            nlohmann::json action = {
                {"object_id", static_cast<int>(obj_idx)},
                {"volume_index", static_cast<int>(vol_idx)},
            };
            const OllamaActionResult r = OllamaMeshOps::apply_repair_mesh(action);
            if (r.success)
                ++repaired;
        }
    }
    return repaired;
}

PrintJobStepExecutors::GeometryResult PrintJobStepExecutors::build_geometry_context()
{
    GeometryResult out;
    Plater* plater = wxGetApp().plater();
    if (!plater || !wxGetApp().preset_bundle)
        return out;

    out.ctx = PrintPlannerGui::build_plate_context(plater);

    // Full assessment recomputes stability + orientation from the live mesh
    // (produce_from_plate_context cannot, since PlateContext carries no mesh).
    const DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    GeometryAssessmentProducer::Options opts;
    opts.printer_id = out.ctx.printer_id;
    const PrinterLearningProfile learning; // default profile; per-printer learning is layered elsewhere

    out.assessment = GeometryAssessmentProducer::produce_for_objects(plater->model().objects, config, learning, opts);
    out.ok         = true;
    return out;
}

ConfigProposal PrintJobStepExecutors::build_and_cache_proposal(const PlateContext& ctx,
                                                               const PrintIntent&  intent,
                                                               bool                korean)
{
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return ConfigProposal{};
    // build_from_context caches into OllamaConfigProposalCache and mirrors into
    // PrintGoalSession, keeping Smart Print + LLM context consistent.
    return OllamaConfigProposalBuilder::build_from_context(plater, ctx, intent, korean);
}

int PrintJobStepExecutors::apply_config_proposal(const ConfigProposal& proposal)
{
    if (!proposal.set_config_actions.is_array() || proposal.set_config_actions.empty())
        return 0;

    nlohmann::json root;
    root["actions"] = proposal.set_config_actions;
    const std::vector<OllamaActionResult> results = OllamaActionExecutor::execute(root);

    int changed = 0;
    for (const OllamaActionResult& r : results)
        if (r.success && r.effective_change)
            ++changed;
    return changed;
}

bool PrintJobStepExecutors::trigger_slice()
{
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return false;
    wxPostEvent(plater, SimpleEvent(EVT_GLTOOLBAR_SLICE_PLATE));
    return true;
}

PrintEstimateSummary PrintJobStepExecutors::collect_estimate(const GeometryAssessment& assessment)
{
    PrintEstimateSummary summary;
    Plater* plater = wxGetApp().plater();
    if (!plater || !wxGetApp().preset_bundle)
        return summary;

    Print&                   print  = plater->get_partplate_list().get_current_fff_print();
    const PrintStatistics&   stats  = print.print_statistics();
    const DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    return PrintEstimateCollector::collect(stats, assessment, config);
}

PrintJobStepExecutors::SendResult PrintJobStepExecutors::trigger_send()
{
    SendResult out;
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return out;

    const OllamaFlowDispatchResult d = OllamaUserFlow::dispatch_coach_action("send_print", plater);
    out.handled         = d.handled;
    out.setup_blocked   = d.setup_blocked;
    out.blocked_message = d.blocked_message;
    return out;
}

}}} // namespace Slic3r::GUI::AIPipeline
