#include "ChangeEffects.hpp"

#include "libslic3r/I18N.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

// "35%", "0.2", "true", "gyroid" -> number when it is one.
bool number(const std::string& s, double& out)
{
    if (s == "true" || s == "1") { out = 1; return true; }
    if (s == "false" || s == "0") { out = 0; return true; }
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != s.c_str();
}

int sign(double d) { return d > 1e-9 ? 1 : d < -1e-9 ? -1 : 0; }

} // namespace

ChangeEffects ChangeEffects::estimate(const std::vector<SettingChange>& changes)
{
    ChangeEffects e;
    for (const SettingChange& c : changes) {
        double a = 0, b = 0;
        const bool numeric = number(c.old_value, a) && number(c.new_value, b);
        const int up = numeric ? sign(b - a) : 0;
        const std::string& k = c.key;

        if (k == "wall_loops" || k == "top_shell_layers" || k == "bottom_shell_layers") {
            e.strength += up; e.time += up; e.filament += up;
        } else if (k == "sparse_infill_density") {
            e.strength += up; e.time += up; e.filament += up;
        } else if (k == "layer_height") {
            // thinner layers: finer surface, much longer print
            e.surface -= up; e.time -= 2 * up;
        } else if (k == "enable_support") {
            e.time += up; e.filament += up; e.surface -= up;
        } else if (k == "brim_width" || k == "brim_type" || k == "enable_brim") {
            const int on = numeric ? up : (c.new_value != "no_brim" ? 1 : -1);
            e.adhesion += on; e.filament += on;
        } else if (k == "bed_temperature" || k == "bed_temperature_initial_layer" || k == "hot_plate_temp" ||
                   k == "hot_plate_temp_initial_layer" || k == "textured_plate_temp") {
            e.adhesion += up;
        } else if (k == "initial_layer_speed" || k == "initial_layer_print_speed") {
            e.adhesion -= up; e.time -= up;
        } else if (k.find("speed") != std::string::npos && k.find("fan") == std::string::npos) {
            e.time -= up; e.surface -= up;
        } else if (k == "ironing_type") {
            const int on = c.new_value != "no ironing" && c.new_value != "no_ironing" ? 1 : -1;
            e.surface += on; e.time += on;
        } else if (k == "spiral_mode") {
            e.time -= up; e.filament -= up; e.strength -= up;
        }
    }
    return e;
}

std::string ChangeEffects::summary() const
{
    std::string out;
    auto add = [&out](const std::string& label, int v, const char* more, const char* less) {
        if (!v)
            return;
        if (!out.empty())
            out += " · ";
        out += label + ": " + _u8L(v > 0 ? more : less);
    };
    add(_u8L("Print time"), time, "longer", "shorter");
    add(_u8L("Strength"), strength, "stronger", "weaker");
    add(_u8L("Filament"), filament, "more", "less");
    add(_u8L("Surface"), surface, "smoother", "rougher");
    add(_u8L("Bed adhesion"), adhesion, "better", "weaker");
    return out;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
