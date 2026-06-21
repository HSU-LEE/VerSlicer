#include "OllamaActionPipeline.hpp"
#include "OllamaActionJsonExtract.hpp"
#include "OllamaActionPipelineCore.hpp"
#include "OllamaActionRegistry.hpp"
#include "OllamaActionValidator.hpp"
#include "OllamaConfig.hpp"
#include "OllamaResponseNormalizer.hpp"

#include "../BambuSmartPrint/PrintPlannerGui.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"

namespace Slic3r { namespace GUI {

namespace {

void filter_advisor_actions(nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object() || !a.contains("type") || !a["type"].is_string())
            continue;
        if (OllamaActionRegistry::is_allowed_in_advisor(a["type"].get<std::string>()))
            kept.push_back(a);
    }
    root["actions"] = std::move(kept);
}

void strip_non_makerworld_actions(nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    nlohmann::json kept = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object() || !a.contains("type") || !a["type"].is_string())
            continue;
        const std::string t = a["type"].get<std::string>();
        if (t == "makerworld_search" || t == "import_makerworld")
            kept.push_back(a);
    }
    root["actions"] = std::move(kept);
}

bool root_actions_empty(const nlohmann::json& root)
{
    return !root.contains("actions") || !root["actions"].is_array() || root["actions"].empty();
}

void merge_planner_if_no_llm_config(nlohmann::json& root, const std::string& user_request)
{
    if (OllamaResponseNormalizer::has_viable_set_config(root))
        return;
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return;
    const BambuSmartPrint::PrintPlan plan = PrintPlannerGui::plan_for_user_text(plater, user_request);
    if (!plan.root.contains("actions") || !plan.root["actions"].is_array() || plan.root["actions"].empty())
        return;
    if ((!root.contains("message") || !root["message"].is_string() || root["message"].get<std::string>().empty())
        && plan.root.contains("message") && plan.root["message"].is_string())
        root["message"] = plan.root["message"];

    nlohmann::json merged = nlohmann::json::array();
    if (root.contains("actions") && root["actions"].is_array()) {
        for (const auto& a : root["actions"]) {
            if (!a.is_object())
                continue;
            const std::string type = a.value("type", "");
            if (type != "set_config")
                merged.push_back(a);
        }
    }
    for (const auto& a : plan.root["actions"])
        merged.push_back(a);
    root["actions"] = std::move(merged);
}

} // namespace

void OllamaActionPipeline::dedupe_actions_in_turn(nlohmann::json& root)
{
    OllamaActionPipelineCore::dedupe_actions_in_turn(root);
}

void OllamaActionPipeline::strip_actions_for_question_history(nlohmann::json& root)
{
    if (root.contains("actions"))
        root.erase("actions");
}

nlohmann::json OllamaActionPipeline::extract_from_assistant_text(const std::string& assistant_text)
{
    return extract_ollama_action_json_with_repair(assistant_text);
}

OllamaPipelineResult OllamaActionPipeline::process_actions(nlohmann::json& root, const OllamaPipelineOptions& opt)
{
    OllamaPipelineResult result;
    if (opt.apply_mode) {
        if (!OllamaResponseNormalizer::has_viable_set_config(root))
            merge_planner_if_no_llm_config(root, opt.user_request);
        result.normalized = OllamaResponseNormalizer::normalize(root, opt.user_request, opt.include_makerworld);
        result.sanitized  = OllamaActionValidator::sanitize(root, opt.user_request);
    } else if (opt.question_mode_strip) {
        strip_non_makerworld_actions(root);
    }
    dedupe_actions_in_turn(root);
    if (opt.advisor_filter)
        filter_advisor_actions(root);
    if (root_actions_empty(root) && root.contains("actions"))
        root.erase("actions");
    result.actions_empty = root_actions_empty(root);
    return result;
}

nlohmann::json OllamaActionPipeline::build_rule_only_root(const std::string& user_request, bool include_makerworld)
{
    if (!ollama_rule_only_fallback_enabled())
        return nlohmann::json{{"message", "Could not parse model reply."}, {"actions", nlohmann::json::array()}};
    nlohmann::json root = nlohmann::json::object();
    root["message"]     = "Applying suggested fixes from your request.";
    root["actions"]     = nlohmann::json::array();
    OllamaPipelineOptions opt;
    opt.apply_mode         = true;
    opt.include_makerworld = include_makerworld;
    opt.user_request       = user_request;
    process_actions(root, opt);
    return root;
}

nlohmann::json OllamaActionPipeline::build_recovery_root(const std::string& assistant_text,
                                                        const std::string& user_request, bool include_makerworld)
{
    nlohmann::json root = try_salvage_ollama_action_json(assistant_text);
    if (root.empty()) {
        root            = nlohmann::json::object();
        root["message"] = "Applying suggested fixes from your request.";
        root["actions"] = nlohmann::json::array();
    }

    OllamaPipelineOptions opt;
    opt.apply_mode         = true;
    opt.include_makerworld = include_makerworld;
    opt.user_request       = user_request;
    process_actions(root, opt);
    return root;
}

bool OllamaActionPipeline::prepare_apply_root(nlohmann::json& root, const std::string& user_request,
                                              bool include_makerworld)
{
    if (!root.is_object())
        root = nlohmann::json::object();
    OllamaPipelineOptions opt;
    opt.apply_mode         = true;
    opt.include_makerworld = include_makerworld;
    opt.user_request       = user_request;
    process_actions(root, opt);
    return root.contains("actions") && root["actions"].is_array() && !root["actions"].empty();
}

}} // namespace
