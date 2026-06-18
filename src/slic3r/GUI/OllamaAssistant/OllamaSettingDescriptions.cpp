#include "OllamaSettingDescriptions.hpp"

#include "OllamaSettingRegistry.hpp"

#include <boost/algorithm/string.hpp>

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
        return ko ? "끔" : "Off";
    if (v == "outer_only" || v == "outeronly")
        return ko ? "바깥 테두리만" : "Outer only";
    if (v == "auto_brim" || v == "autobrim")
        return ko ? "자동" : "Auto";
    if (v == "inner_only")
        return ko ? "안쪽 테두리만" : "Inner only";
    return raw;
}

bool message_mentions_adhesion(const std::string& msg)
{
    return msg.find("붙") != std::string::npos || msg.find("브림") != std::string::npos
        || msg.find("베드") != std::string::npos || msg.find("들") != std::string::npos
        || boost::icontains(msg, "adhesion") || boost::icontains(msg, "brim") || boost::icontains(msg, "stick");
}

bool message_mentions_speed(const std::string& msg)
{
    return msg.find("오래") != std::string::npos || msg.find("가속") != std::string::npos
        || msg.find("빠르") != std::string::npos || msg.find("속도") != std::string::npos
        || boost::icontains(msg, "speed") || boost::icontains(msg, "faster") || boost::icontains(msg, "acceler");
}

bool message_mentions_strength(const std::string& msg)
{
    return msg.find("단단") != std::string::npos || msg.find("강") != std::string::npos
        || msg.find("부서") != std::string::npos || boost::icontains(msg, "strong") || boost::icontains(msg, "strength");
}

bool message_mentions_support(const std::string& msg)
{
    return msg.find("서포트") != std::string::npos || msg.find("공중") != std::string::npos
        || boost::icontains(msg, "support") || boost::icontains(msg, "overhang");
}

bool contains_raw_config_jargon(const std::string& msg)
{
    return msg.find("brim_width") != std::string::npos || msg.find("sparse_infill") != std::string::npos
        || msg.find("wall_loops") != std::string::npos || msg.find("enable_support") != std::string::npos
        || msg.find("brim_type") != std::string::npos || msg.find("layer_height") != std::string::npos;
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

bool assistant_message_mismatch(const std::string& msg, const std::vector<BambuSmartPrint::SettingChange>& changes)
{
    if (msg.empty() || changes.empty())
        return false;

    bool adhesion = false;
    bool speed    = false;
    bool strength = false;
    bool support  = false;
    for (const auto& ch : changes) {
        adhesion |= change_is_adhesion(ch.key);
        speed |= change_is_speed(ch.key);
        strength |= change_is_strength(ch.key);
        support |= change_is_support(ch.key);
    }

    if (adhesion && message_mentions_speed(msg) && !message_mentions_adhesion(msg))
        return true;
    if (speed && !adhesion && message_mentions_adhesion(msg) && !message_mentions_speed(msg))
        return true;
    if (strength && message_mentions_speed(msg) && !message_mentions_strength(msg))
        return true;
    if (support && !message_mentions_support(msg) && message_mentions_speed(msg))
        return true;
    return false;
}

std::string reason_for_brim_width(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v <= 0.0 || is_falsy(ch.new_value))
        return ko ? "바닥 테두리(brim)를 끄고 베드 접착 보조를 줄입니다."
                  : "Turn off the brim to reduce extra material around the base.";
    if (new_v > old_v)
        return ko ? "바닥 가장자리에 플라스틱을 더 깔아 베드에 잘 달라붙게 합니다."
                  : "Add extra plastic around the base so the first layer grips the bed better.";
    return ko ? "브림 너비를 줄여 출력 시간과 필라멘트 사용량을 낮춥니다."
              : "Narrow the brim to save time and filament.";
}

std::string reason_for_brim_type(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    if (is_falsy(ch.new_value) || ch.new_value == "no_brim")
        return ko ? "브림을 끕니다." : "Disable brim.";
    return ko ? "브림 방식을 바꿔 베드 접착과 모델 형태에 맞춥니다."
              : "Adjust brim style for bed adhesion and model shape.";
}

std::string reason_for_enable_support(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    if (is_truthy(ch.new_value))
        return ko ? "공중으로 나오는 부분을 받쳐 줄 서포트를 켭니다."
                  : "Enable supports to hold overhanging sections during printing.";
    return ko ? "서포트를 끄고 출력 후처리를 줄입니다." : "Turn off supports to reduce material and cleanup.";
}

std::string reason_for_infill(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v > old_v)
        return ko ? "안쪽을 더 꽉 채워 부품을 단단하게 만듭니다."
                  : "Increase infill so the part is stronger and less brittle.";
    return ko ? "안쪽 채움을 줄여 출력 시간과 필라멘트를 절약합니다."
              : "Lower infill to reduce print time and filament use.";
}

std::string reason_for_walls(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v > old_v)
        return ko ? "벽을 한 겹 더 두껍게 해 힘을 더 잘 받게 합니다."
                  : "Add an extra wall so the part handles stress better.";
    return ko ? "벽 겹 수를 줄여 출력 시간을 단축합니다." : "Use fewer walls to shorten print time.";
}

std::string reason_for_layer_height(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v > old_v)
        return ko ? "층 높이를 키워 같은 높이를 더 빨리 출력합니다."
                  : "Use thicker layers to print the same height faster.";
    return ko ? "층 높이를 낮춰 표면을 더 매끈하게 만듭니다."
              : "Use thinner layers for smoother surfaces.";
}

std::string reason_for_speed(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v < old_v)
        return ko ? "속도를 낮춰 품질과 안정성을 높입니다." : "Slow down printing for better quality and reliability.";
    return ko ? "속도를 올려 출력 시간을 줄입니다." : "Increase speed to shorten print time.";
}

std::string reason_for_temperature(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (ch.key.find("bed") != std::string::npos) {
        if (new_v > old_v)
            return ko ? "베드 온도를 올려 첫 층 접착을 돕습니다."
                      : "Raise bed temperature to improve first-layer adhesion.";
        return ko ? "베드 온도를 낮춰 warping 위험을 줄입니다." : "Lower bed temperature to reduce warping risk.";
    }
    if (new_v > old_v)
        return ko ? "노zzle 온도를 올려 층 간 접착을 강화합니다."
                  : "Raise nozzle temperature for better layer bonding.";
    return ko ? "노zzle 온도를 낮춰 stringing을 줄입니다." : "Lower nozzle temperature to reduce stringing.";
}

std::string reason_for_retraction(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    const double old_v = parse_number(ch.old_value);
    const double new_v = parse_number(ch.new_value);
    if (new_v > old_v)
        return ko ? "리트랙션을 늘려 실(stringing)을 줄입니다."
                  : "Increase retraction to reduce stringing between moves.";
    return ko ? "리트랙션을 줄여 필라멘트 마모를 낮춥니다." : "Reduce retraction to limit filament grinding.";
}

} // namespace

std::string OllamaSettingDescriptions::setting_label(const std::string& key, bool korean)
{
    if (const OllamaSettingSpec* sp = OllamaSettingRegistry::find_spec(key))
        return korean ? sp->desc_ko : sp->desc_en;
    return key;
}

std::string OllamaSettingDescriptions::format_value(const std::string& key, const std::string& raw, bool korean)
{
    if (key == "enable_support" || key == "enable_brim" || key == "support_on_build_plate_only")
        return is_truthy(raw) ? (korean ? "켬" : "On") : (korean ? "끔" : "Off");
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

    if (korean)
        return setting_label(key, true) + " 값을 " + format_value(key, change.old_value, true) + "에서 "
             + format_value(key, change.new_value, true) + "로 바꿉니다.";
    return "Change " + setting_label(key, false) + " from " + format_value(key, change.old_value, false) + " to "
         + format_value(key, change.new_value, false) + ".";
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
    if (korean) {
        for (size_t i = 0; i < changes.size(); ++i) {
            if (i > 0)
                oss << (i + 1 == changes.size() ? " 또한 " : ", ");
            oss << change_reason(changes[i], true);
        }
    } else {
        for (size_t i = 0; i < changes.size(); ++i) {
            if (i > 0)
                oss << (i + 1 == changes.size() ? " Also, " : " ");
            oss << change_reason(changes[i], false);
        }
    }
    const std::string synthesized = oss.str();

    if (assistant_message.empty() || contains_raw_config_jargon(assistant_message)
        || assistant_message_mismatch(assistant_message, changes))
        return synthesized;

    return assistant_message;
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
        out.push_back(korean ? "첫 층이 베드에 더 잘 붙을 가능성이 높습니다."
                             : "The first layer should stick to the bed more reliably.");
    if (stronger)
        out.push_back(korean ? "부품이 더 단단해질 수 있습니다." : "The part should be stronger and less brittle.");
    if (faster)
        out.push_back(korean ? "출력 시간이 다소 줄어들 수 있습니다." : "Print time may be slightly shorter.");
    if (adds_time && !faster)
        out.push_back(korean ? "출력 시간이 조금 늘어날 수 있습니다." : "Print time may be slightly longer.");
    if (adds_material)
        out.push_back(korean ? "필라멘트 사용량이 조금 늘어날 수 있습니다." : "Filament use may increase slightly.");

    return out;
}

}} // namespace
