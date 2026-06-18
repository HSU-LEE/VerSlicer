#ifndef slic3r_OllamaBenchmarkScenarios_hpp_
#define slic3r_OllamaBenchmarkScenarios_hpp_

#include <functional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct OllamaBenchmarkScenario
{
    const char*                    id;
    const char*                    user_request;
    std::function<bool(const std::string&)> check; // receives normalized pipeline hint or action type
};

/** Golden scenarios for offline regression (intent rules, search, pipeline). */
const std::vector<OllamaBenchmarkScenario>& ollama_benchmark_scenarios();

}} // namespace

#endif
