#include "OllamaSettingDescriptions.hpp"
#include "AiCatalog.hpp"

#include "OllamaSettingRegistry.hpp"
#include "../I18N.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <cmath>
#include <sstream>

namespace Slic3r { namespace GUI {

namespace {

double parse_number(const std::string& raw)
{
    if (raw.empty())
        return 0.0;
    try {
        std::string s = raw;
        boost::algorithm::trim(s);
        if (!s.empty() && s.back() == '%')
            s.pop_back();
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

bool is_truthy(const std::string& raw)
{
    std::string s = raw;
    boost::algorithm::to_lower(s);
    boost::algorithm::trim(s);
    return s == "1" || s == "true" || s == "on" || s == "yes";
}

bool is_falsy(const std::string& raw)
{
    std::string s = raw;
    boost::algorithm::to_lower(s);
    boost::algorithm::trim(s);
    return s.empty() || s == "0" || s == "false" || s == "off" || s == "no" || s == "no_brim";
}

std::string unit_suffix(const std::string& key)
{
    if (const OllamaSettingSpec* sp = OllamaSettingRegistry::find_spec(key)) {
        if (sp->unit && sp->unit[0] != '\0')
            return sp->unit;
    }
    if (key.find("temperature") != std::string::npos)
        return "°C";
    if (key.find("speed") != std::string::npos)
        return " mm/s";
    if (key == "brim_width" || key.find("_mm") != std::string::npos)
        return " mm";
    return {};
}

std::string format_brim_type(const std::string& raw, bool ko)
{
    std::string v = raw;
    boost::algorithm::to_lower(v);
    if (v == "no_brim" || v == "none" || v == "0")
        return ai_catalog_text("Off");
    if (v == "outer_only" || v == "outeronly")
        return ai_catalog_text("Outer only");
    if (v == "auto_brim" || v == "autobrim")
        return ai_catalog_text("Auto");
    if (v == "inner_only")
        return ai_catalog_text("Inner only");
    return raw;
}

bool change_is_adhesion(const std::string& key)
{
    return key.find("brim") != std::string::npos || key.find("bed") != std::string::npos
        || key.find("first_layer") != std::string::npos || key == "initial_layer_print_height";
}

bool change_is_speed(const std::string& key)
{
    return key.find("speed") != std::string::npos || key == "layer_height";
}

bool change_is_strength(const std::string& key)
{
    return key == "sparse_infill_density" || key == "wall_loops" || key == "top_shell_layers"
        || key == "bottom_shell_layers";
}

bool change_is_support(const std::string& key)
{
    return key.find("support") != std::string::npos;
}

std::string reason_for_brim_width(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v <= 0.0 || is_falsy(ch.new_value))
        return ai_catalog_text("Turn off the brim to reduce extra material around the base.");
    if (new_v > old_v)
        return ai_catalog_text("Add extra plastic around the base so the first layer grips the bed better.");
    return ai_catalog_text("Narrow the brim to save time and filament.");
}

std::string reason_for_brim_type(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    if (is_falsy(ch.new_value) || ch.new_value == "no_brim")
        return ai_catalog_text("Disable brim.");
    return ai_catalog_text("Adjust brim style for bed adhesion and model shape.");
}

std::string reason_for_enable_support(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    if (is_truthy(ch.new_value))
        return ai_catalog_text("Enable supports to hold overhanging sections during printing.");
    return ai_catalog_text("Turn off supports to reduce material and cleanup.");
}

std::string reason_for_infill(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v > old_v)
        return ai_catalog_text("Increase infill so the part is stronger and less brittle.");
    return ai_catalog_text("Lower infill to reduce print time and filament use.");
}

std::string reason_for_walls(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v > old_v)
        return ai_catalog_text("Add an extra wall so the part handles stress better.");
    return ai_catalog_text("Use fewer walls to shorten print time.");
}

std::string reason_for_layer_height(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v > old_v)
        return ai_catalog_text("Use thicker layers to print the same height faster.");
    return ai_catalog_text("Use thinner layers for smoother surfaces.");
}

std::string reason_for_speed(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v < old_v)
        return ai_catalog_text("Slow down printing for better quality and reliability.");
    return ai_catalog_text("Increase speed to shorten print time.");
}

std::string reason_for_temperature(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (ch.key.find("bed") != std::string::npos) {
        if (new_v > old_v)
            return ai_catalog_text("Raise bed temperature to improve first-layer adhesion.");
        return ai_catalog_text("Lower bed temperature to reduce warping risk.");
    }
    if (new_v > old_v)
        return ai_catalog_text("Raise nozzle temperature for better layer bonding.");
    return ai_catalog_text("Lower nozzle temperature to reduce stringing.");
}

std::string reason_for_retraction(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v > old_v)
        return ai_catalog_text("Increase retraction to reduce stringing between moves.");
    return ai_catalog_text("Reduce retraction to limit filament grinding.");
}

} // namespace

std::string OllamaSettingDescriptions::setting_label(const std::string& key, bool korean)
{
    if (const OllamaSettingSpec* sp = OllamaSettingRegistry::find_spec(key))
        return ai_catalog_text(sp->desc_en);
    return key;
}

std::string OllamaSettingDescriptions::format_value(const std::string& key, const std::string& raw, bool korean)
{
    if (key == "enable_support" || key == "enable_brim" || key == "support_on_build_plate_only")
        return is_truthy(raw) ? (ai_catalog_text("On")) : (ai_catalog_text("Off"));
    if (key == "brim_type")
        return format_brim_type(raw, korean);

    const std::string unit = unit_suffix(key);
    if (!unit.empty() && !raw.empty() && raw.find(unit) == std::string::npos) {
        if (key == "sparse_infill_density" && raw.find('%') == std::string::npos)
            return raw + "%";
        if (unit == " mm/s" || unit == " mm" || unit == "°C")
            return raw + unit;
    }
    return raw;
}

std::string OllamaSettingDescriptions::change_reason(const BambuSmartPrint::SettingChange& change, bool korean)
{
    if (!change.reason.empty() && change.reason != "AI assistant suggestion")
        return change.reason;

    const std::string& key = change.key;
    if (key == "brim_width" || key == "enable_brim")
        return reason_for_brim_width(change, korean);
    if (key == "brim_type")
        return reason_for_brim_type(change, korean);
    if (key == "enable_support")
        return reason_for_enable_support(change, korean);
    if (key == "sparse_infill_density")
        return reason_for_infill(change, korean);
    if (key == "wall_loops")
        return reason_for_walls(change, korean);
    if (key == "layer_height" || key == "initial_layer_print_height")
        return reason_for_layer_height(change, korean);
    if (key.find("speed") != std::string::npos)
        return reason_for_speed(change, korean);
    if (key.find("temperature") != std::string::npos)
        return reason_for_temperature(change, korean);
    if (key.find("retraction") != std::string::npos)
        return reason_for_retraction(change, korean);
    if (key.find("support") != std::string::npos)
        return reason_for_enable_support(change, korean);

    return (boost::format(ai_catalog_text("Change %s from %s to %s."))
            % setting_label(key, korean) % format_value(key, change.old_value, korean)
            % format_value(key, change.new_value, korean)).str();
}

std::string OllamaSettingDescriptions::preview_line(const BambuSmartPrint::SettingChange& change, bool korean)
{
    const std::string label = setting_label(change.key, korean);
    const std::string from  = format_value(change.key, change.old_value, korean);
    const std::string to    = format_value(change.key, change.new_value, korean);
    const std::string why   = change_reason(change, korean);

    if (!from.empty() && !to.empty() && from != to)
        return label + ": " + from + " → " + to + " — " + why;
    if (!to.empty())
        return label + ": " + to + " — " + why;
    return label + " — " + why;
}

std::string OllamaSettingDescriptions::build_summary(const std::vector<BambuSmartPrint::SettingChange>& changes,
                                                     const std::string& assistant_message, bool korean)
{
    if (changes.empty())
        return assistant_message;

    std::ostringstream oss;
    for (size_t i = 0; i < changes.size(); ++i) {
        if (i > 0)
            oss << (i + 1 == changes.size() ? ai_catalog_text(" Also, ") : ", ");
        oss << change_reason(changes[i], korean);
    }
    const std::string synthesized = oss.str();

    return assistant_message.empty() ? synthesized : assistant_message;
}

std::vector<std::string> OllamaSettingDescriptions::expected_effects(
    const std::vector<BambuSmartPrint::SettingChange>& changes, bool korean)
{
    std::vector<std::string> out;
    bool adds_time     = false;
    bool adds_material = false;
    bool better_stick  = false;
    bool stronger      = false;
    bool faster        = false;

    for (const auto& ch : changes) {
        if (change_is_adhesion(ch.key)) {
            const double new_v = parse_number(ch.new_value);
            if (new_v > 0 && !is_falsy(ch.new_value))
                better_stick = adds_material = adds_time = true;
        }
        if (change_is_support(ch.key) && is_truthy(ch.new_value))
            adds_material = adds_time = true;
        if (change_is_strength(ch.key)) {
            const double new_v = parse_number(ch.new_value);
            const double old_v = parse_number(ch.old_value);
            if (new_v > old_v)
                stronger = adds_time = adds_material = true;
        }
        if (change_is_speed(ch.key)) {
            const double new_v = parse_number(ch.new_value);
            const double old_v = parse_number(ch.old_value);
            if (ch.key == "layer_height") {
                if (new_v > old_v)
                    faster = true;
            } else if (new_v > old_v) {
                faster = true;
            } else if (new_v < old_v) {
                adds_time = true;
            }
        }
    }

    if (better_stick)
        out.push_back(ai_catalog_text("The first layer should stick to the bed more reliably."));
    if (stronger)
        out.push_back(ai_catalog_text("The part should be stronger and less brittle."));
    if (faster)
        out.push_back(ai_catalog_text("Print time may be slightly shorter."));
    if (adds_time && !faster)
        out.push_back(ai_catalog_text("Print time may be slightly longer."));
    if (adds_material)
        out.push_back(ai_catalog_text("Filament use may increase slightly."));

    return out;
}

}} // namespace
