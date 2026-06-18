#include "OllamaIntentRules.hpp"

#include <boost/algorithm/string.hpp>
#include <regex>

namespace Slic3r { namespace GUI {
namespace OllamaIntentRules {

bool contains_support_intent(const std::string& s)
{
    return s.find("서포트") != std::string::npos || s.find("support") != std::string::npos;
}

bool contains_midair_or_failure_intent(const std::string& s)
{
    return s.find("공중") != std::string::npos || s.find("실패") != std::string::npos ||
           s.find("망") != std::string::npos || s.find("안되") != std::string::npos ||
           s.find("매달") != std::string::npos || s.find("떨어") != std::string::npos ||
           s.find("떠서") != std::string::npos || s.find("오버행") != std::string::npos ||
           s.find("failed") != std::string::npos || s.find("failure") != std::string::npos ||
           s.find("mid-air") != std::string::npos || s.find("mid air") != std::string::npos ||
           s.find("overhang") != std::string::npos || s.find("floating") != std::string::npos ||
           s.find("sagging") != std::string::npos;
}

bool contains_adhesion_intent(const std::string& s)
{
    return s.find("들뜸") != std::string::npos || s.find("안 붙") != std::string::npos ||
           s.find("안붙") != std::string::npos || s.find("베드") != std::string::npos ||
           s.find("접착") != std::string::npos || s.find("warp") != std::string::npos ||
           s.find("curl") != std::string::npos || s.find("stick") != std::string::npos ||
           s.find("adhesion") != std::string::npos || s.find("lift") != std::string::npos;
}

bool contains_strength_intent(const std::string& s)
{
    return s.find("단단") != std::string::npos || s.find("튼튼") != std::string::npos ||
           s.find("꽉") != std::string::npos || s.find("속이") != std::string::npos ||
           s.find("stronger") != std::string::npos || s.find("solid") != std::string::npos ||
           s.find("sturdy") != std::string::npos;
}

bool contains_explicit_infill_intent(const std::string& s)
{
    return s.find("채움") != std::string::npos || s.find("infill") != std::string::npos ||
           s.find("%") != std::string::npos;
}

bool contains_flip_intent(const std::string& s)
{
    return s.find("뒤집") != std::string::npos || s.find("flip") != std::string::npos;
}

bool contains_rotate_intent(const std::string& s)
{
    if (s.find("돌려") != std::string::npos || s.find("회전") != std::string::npos ||
        s.find("rotate") != std::string::npos || s.find("rotation") != std::string::npos)
        return true;
    static const std::regex re_deg(R"((\d+(?:\.\d+)?)\s*도)");
    static const std::regex re_en(R"((\d+(?:\.\d+)?)\s*(?:degrees|deg))", std::regex::icase);
    return std::regex_search(s, re_deg) || std::regex_search(s, re_en);
}

bool contains_placement_intent(const std::string& s)
{
    return s.find("배치") != std::string::npos || s.find("arrange") != std::string::npos ||
           s.find("정렬") != std::string::npos || s.find("place") != std::string::npos ||
           s.find("위치") != std::string::npos || s.find("옮겨") != std::string::npos;
}

bool user_wants_delete(const std::string& user)
{
    return user.find("delete") != std::string::npos || user.find("remove") != std::string::npos ||
           user.find("erase") != std::string::npos || user.find("삭제") != std::string::npos ||
           user.find("지워") != std::string::npos || user.find("지우") != std::string::npos;
}

bool user_wants_plate_arrange(const std::string& user)
{
    return contains_placement_intent(user);
}

bool contains_disable_brim_intent(const std::string& s)
{
    return s.find("no brim") != std::string::npos || s.find("without brim") != std::string::npos ||
           s.find("disable brim") != std::string::npos || s.find("brim off") != std::string::npos ||
           s.find("브림 끄") != std::string::npos || s.find("브림 해제") != std::string::npos ||
           s.find("브림 없") != std::string::npos || s.find("브림 비활성") != std::string::npos ||
           s.find("브림 꺼") != std::string::npos;
}

bool contains_brim_intent(const std::string& s)
{
    if (contains_disable_brim_intent(s))
        return false;
    return s.find("brim") != std::string::npos || s.find("브림") != std::string::npos;
}

bool contains_durability_intent(const std::string& s)
{
    return s.find("파손") != std::string::npos || s.find("부서") != std::string::npos ||
           s.find("부러") != std::string::npos || s.find("깨") != std::string::npos ||
           s.find("약해") != std::string::npos || s.find("쉽게") != std::string::npos ||
           s.find("망가") != std::string::npos || s.find("힘없") != std::string::npos ||
           s.find("fragile") != std::string::npos || s.find("brittle") != std::string::npos ||
           s.find("break") != std::string::npos || s.find("breaks") != std::string::npos ||
           s.find("snaps") != std::string::npos;
}

bool describes_print_quality_symptom(const std::string& s)
{
    return contains_durability_intent(s) || contains_adhesion_intent(s) ||
           contains_strength_intent(s) || contains_midair_or_failure_intent(s);
}

std::optional<double> parse_z_rotation_degrees(const std::string& s)
{
    static const std::regex re_deg(R"((\d+(?:\.\d+)?)\s*도)");
    static const std::regex re_en(R"((\d+(?:\.\d+)?)\s*(?:degrees|deg))", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(s, m, re_deg) && !std::regex_search(s, m, re_en))
        return std::nullopt;
    const double deg = std::stod(m[1].str());
    const bool right = s.find("우측") != std::string::npos || s.find("오른쪽") != std::string::npos ||
                       s.find("right") != std::string::npos || s.find("clockwise") != std::string::npos;
    const bool left = s.find("좌측") != std::string::npos || s.find("왼쪽") != std::string::npos ||
                      s.find("left") != std::string::npos || s.find("counter") != std::string::npos;
    if (right && !left)
        return deg;
    if (left && !right)
        return -deg;
    return deg;
}

std::string extract_first_url_from_text(const std::string& text)
{
    static const std::regex link_re(R"((https?://[^\s]+))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, link_re))
        return m[1].str();
    return {};
}

} // namespace OllamaIntentRules
}} // namespace
