#include "OllamaEvalGrader.hpp"

#include "OllamaActionJsonExtract.hpp"

#include <algorithm>
#include <set>

namespace Slic3r { namespace GUI {

namespace {

std::vector<std::string> string_list(const nlohmann::json& j, const char* key)
{
    std::vector<std::string> out;
    if (j.contains(key) && j[key].is_array())
        for (const auto& v : j[key])
            if (v.is_string())
                out.push_back(v.get<std::string>());
    return out;
}

} // namespace

std::vector<OllamaEvalCase> OllamaEvalGrader::parse_cases(const nlohmann::json& doc)
{
    std::vector<OllamaEvalCase> cases;
    if (!doc.contains("cases") || !doc["cases"].is_array())
        return cases;
    for (const auto& j : doc["cases"]) {
        if (!j.is_object())
            continue;
        OllamaEvalCase c;
        c.id             = j.value("id", "");
        c.request        = j.value("request", "");
        c.expect_actions = string_list(j, "expect_actions");
        c.expect_keys    = string_list(j, "expect_keys");
        c.forbid_actions = string_list(j, "forbid_actions");
        if (!c.id.empty() && !c.request.empty())
            cases.push_back(std::move(c));
    }
    return cases;
}

OllamaEvalVerdict OllamaEvalGrader::grade(const OllamaEvalCase& c, const std::string& reply_text)
{
    nlohmann::json root;
    try {
        root = extract_ollama_action_json_with_repair(reply_text);
    } catch (const std::exception&) {
        return {false, "reply is not parseable JSON"};
    }

    std::set<std::string> types;
    std::set<std::string> keys;
    if (root.contains("actions") && root["actions"].is_array()) {
        for (const auto& a : root["actions"]) {
            if (!a.is_object() || !a.contains("type") || !a["type"].is_string())
                continue;
            types.insert(a["type"].get<std::string>());
            if (a.contains("options") && a["options"].is_object())
                for (auto it = a["options"].begin(); it != a["options"].end(); ++it)
                    keys.insert(it.key());
        }
    }

    for (const std::string& t : c.expect_actions)
        if (!types.count(t))
            return {false, "missing action " + t};
    for (const std::string& k : c.expect_keys)
        if (!keys.count(k))
            return {false, "missing set_config key " + k};
    for (const std::string& t : c.forbid_actions)
        if (types.count(t))
            return {false, "forbidden action " + t};
    return {true, {}};
}

const OllamaEvalCase* OllamaEvalGrader::match_case(const std::vector<OllamaEvalCase>& cases,
                                                   const std::string& user_message)
{
    // The recorded user message carries plate context around the request, so
    // match by containment and prefer the longest request to avoid "brim" vs
    // "no brim" collisions.
    const OllamaEvalCase* best = nullptr;
    for (const auto& c : cases)
        if (user_message.find(c.request) != std::string::npos && (!best || c.request.size() > best->request.size()))
            best = &c;
    return best;
}

std::map<std::string, OllamaEvalScore> OllamaEvalGrader::score_log(const std::vector<OllamaEvalCase>& cases,
                                                                   const std::vector<nlohmann::json>& records)
{
    std::map<std::string, std::map<std::string, std::string>> latest; // model -> case id -> reply
    for (const auto& r : records) {
        if (!r.is_object())
            continue;
        const OllamaEvalCase* c = match_case(cases, r.value("user", ""));
        if (c)
            latest[r.value("model", "unknown")][c->id] = r.value("reply", "");
    }

    std::map<std::string, OllamaEvalScore> scores;
    for (const auto& [model, replies] : latest) {
        OllamaEvalScore& s = scores[model];
        for (const auto& c : cases) {
            const auto it = replies.find(c.id);
            if (it == replies.end())
                continue;
            ++s.total;
            const OllamaEvalVerdict v = grade(c, it->second);
            if (v.pass)
                ++s.passed;
            else
                s.failures[c.id] = v.reason;
        }
    }
    return scores;
}

}} // namespace
