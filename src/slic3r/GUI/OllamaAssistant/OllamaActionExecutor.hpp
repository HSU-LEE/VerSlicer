#ifndef slic3r_OllamaActionExecutor_hpp_
#define slic3r_OllamaActionExecutor_hpp_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

struct OllamaActionResult
{
    bool        success{false};
    std::string message;
};

class OllamaActionExecutor
{
public:
    static std::string build_system_prompt();
    static std::string build_context_json();

    /** Extract JSON object from assistant text (fenced block or raw). */
    static nlohmann::json extract_action_json(const std::string& assistant_text);

    static std::vector<OllamaActionResult> execute(const nlohmann::json& root);
};

}} // namespace

#endif
