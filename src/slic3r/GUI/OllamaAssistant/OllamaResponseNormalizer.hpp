#ifndef slic3r_OllamaResponseNormalizer_hpp_
#define slic3r_OllamaResponseNormalizer_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct OllamaNormalizeResult
{
    std::vector<std::string> warnings;
};

class OllamaResponseNormalizer
{
public:
    /** Patch/coerce LLM JSON using user intent heuristics (shared by chat and model-load advisor). */
    static OllamaNormalizeResult normalize(nlohmann::json& root, const std::string& user_request,
                                           bool include_makerworld = true,
                                           bool force_user_intent  = false);
    static void drop_redundant_slice_actions(nlohmann::json& root);
};

}} // namespace

#endif
