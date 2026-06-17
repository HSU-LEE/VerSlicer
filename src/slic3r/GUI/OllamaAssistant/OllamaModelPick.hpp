#ifndef slic3r_OllamaModelPick_hpp_
#define slic3r_OllamaModelPick_hpp_

#include <boost/algorithm/string.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

inline std::string pick_installed_ollama_model(const std::vector<std::string>& models, std::string want)
{
    boost::trim(want);
    if (want.empty())
        want = "llama3.2:latest";
    if (want == "llama3.2")
        want = "llama3.2:latest";

    auto exact = [&](const std::string& n) { return n == want; };
    auto prefix = [&](const std::string& n) { return n.rfind(want + ":", 0) == 0; };

    for (const auto& n : models) {
        if (exact(n))
            return n;
    }
    for (const auto& n : models) {
        if (prefix(n))
            return n;
    }
    for (const auto& n : models) {
        if (n.rfind("llama3.2", 0) == 0)
            return n;
    }
    for (const auto& n : models) {
        if (n.find("llama") != std::string::npos)
            return n;
    }
    if (!models.empty())
        return models.front();
    return want;
}

}} // namespace

#endif
