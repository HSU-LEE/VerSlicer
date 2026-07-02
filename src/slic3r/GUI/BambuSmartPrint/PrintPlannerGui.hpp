#ifndef slic3r_PrintPlannerGui_hpp_
#define slic3r_PrintPlannerGui_hpp_

#include "libslic3r/BambuSmartPrint/PrintPlanner.hpp"
#include "libslic3r/BambuSmartPrint/PrintGoalSession.hpp"

#include <nlohmann/json.hpp>
#include <vector>

namespace Slic3r { namespace GUI {

class Plater;
struct AICoachCard;
struct SmartPrintWorkflowContent;

/** GUI bridge: build plate context, dispatch plans, fan out to Coach / Smart Print / chat. */
class PrintPlannerGui
{
public:
    static BambuSmartPrint::PlateContext build_plate_context(Plater* plater);

    static BambuSmartPrint::PrintPlan plan_for_user_text(Plater* plater, const std::string& user_text);
    static BambuSmartPrint::PrintPlan plan_from_assistant(Plater* plater, const std::string& user_text,
                                                          const nlohmann::json& assistant_root);

    /** Unified model-load entry: one plan, fan-out to subsystems. */
    static void dispatch_model_loaded(Plater* plater);

    static void apply_plan_to_service(const BambuSmartPrint::PrintPlan& plan);

    static std::vector<AICoachCard> coach_cards_from_plan(Plater* plater,
                                                          const BambuSmartPrint::PrintPlan& plan);
    static SmartPrintWorkflowContent workflow_content_from_plan(const BambuSmartPrint::PrintPlan& plan);

    /** Summary, gauge, and preview lines derived from pending setting changes. */
    static void enrich_workflow_content(SmartPrintWorkflowContent& content,
                                        const std::vector<BambuSmartPrint::SettingChange>& changes,
                                        const BambuSmartPrint::ReadinessReport& readiness,
                                        const std::string& fallback_message = {});

    static BambuSmartPrint::ApplyPolicy default_apply_policy();
};

}} // namespace

#endif
