#ifndef slic3r_OllamaDiagnosticPipeline_hpp_
#define slic3r_OllamaDiagnosticPipeline_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Parsed output from step 1 (problem diagnosis LLM turn). */
struct OllamaDiagnosis
{
    nlohmann::json            raw;
    std::string               symptom;
    std::string               diagnosis;
    std::vector<std::string>  likely_causes;
    std::vector<std::string>  wiki_queries;
    std::vector<std::string>  candidate_keys;
    std::string               user_message;
};

/** Apply-mode pipeline: diagnosis → wiki evidence → settings analysis → proposal. */
class OllamaDiagnosticPipeline
{
public:
    /** Step 2: fetch Bambu Wiki excerpts from diagnosis queries (+ user fallback). */
    static nlohmann::json build_wiki_evidence(const OllamaDiagnosis& diagnosis, const std::string& user_request,
                                              bool korean);

    /** Step 3: analyze current preset values for candidate keys (no LLM). */
    static nlohmann::json analyze_current_settings(const std::vector<std::string>& candidate_keys,
                                                   const OllamaDiagnosis& diagnosis, bool korean);
};

}} // namespace

#endif
