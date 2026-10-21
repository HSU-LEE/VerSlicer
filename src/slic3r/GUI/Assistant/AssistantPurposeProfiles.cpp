#include "AssistantPurposeProfiles.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

// Values are relative to a 0.4 mm nozzle and a standard 0.20 mm process; they
// change only what the purpose actually needs and leave the rest of the
// user's preset alone.
const std::vector<PurposeProfile>& AssistantPurposeProfiles::all()
{
    static const std::vector<PurposeProfile> profiles = {
        {"decorative", "Figure or decoration",
         "Looks first: finer layers and a light infill; strength is not needed.",
         {{"layer_height", 0.16}, {"wall_loops", 2}, {"sparse_infill_density", "10%"},
          {"top_shell_layers", 5}, {"bottom_shell_layers", 3}}},
        {"functional", "Functional or mechanical part",
         "Strength first: more walls, denser gyroid infill and thicker top and bottom.",
         {{"wall_loops", 4}, {"sparse_infill_density", "35%"}, {"sparse_infill_pattern", "gyroid"},
          {"top_shell_layers", 5}, {"bottom_shell_layers", 5}}},
        {"toy", "Toy or something handled a lot",
         "Survives drops and handling: extra walls and a medium infill.",
         {{"wall_loops", 3}, {"sparse_infill_density", "25%"}, {"sparse_infill_pattern", "gyroid"},
          {"top_shell_layers", 5}, {"bottom_shell_layers", 4}}},
        {"vase", "Vase or single-wall container",
         "One continuous spiral wall, no infill and no top: smooth and fast.",
         {{"spiral_mode", true}, {"wall_loops", 1}, {"sparse_infill_density", "0%"},
          {"top_shell_layers", 0}, {"bottom_shell_layers", 3}}},
        {"prototype", "Quick draft or fit test",
         "Speed first: thick layers and a sparse infill to check size and fit.",
         {{"layer_height", 0.28}, {"wall_loops", 2}, {"sparse_infill_density", "10%"},
          {"top_shell_layers", 3}, {"bottom_shell_layers", 3}}},
        {"gift", "Gift or display piece",
         "Best finish: fine layers and an ironed top surface.",
         {{"layer_height", 0.12}, {"wall_loops", 3}, {"sparse_infill_density", "15%"},
          {"ironing_type", "top"}, {"top_shell_layers", 6}}},
    };
    return profiles;
}

std::optional<PurposeProfile> AssistantPurposeProfiles::find(const std::string& id)
{
    const std::string want = boost::algorithm::to_lower_copy(boost::algorithm::trim_copy(id));
    for (const auto& p : all())
        if (p.id == want)
            return p;
    return std::nullopt;
}

std::optional<nlohmann::json> AssistantPurposeProfiles::to_set_config(const std::string& id)
{
    const auto p = find(id);
    if (!p)
        return std::nullopt;
    return nlohmann::json{{"type", "set_config"}, {"preset", "print"}, {"options", p->options},
                          {"purpose", p->id}, {"reason", p->why}};
}

std::vector<std::string> AssistantPurposeProfiles::ids()
{
    std::vector<std::string> out;
    for (const auto& p : all())
        out.push_back(p.id);
    return out;
}

}} // namespace Slic3r::GUI
