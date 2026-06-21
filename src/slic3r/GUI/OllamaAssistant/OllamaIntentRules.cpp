#include "OllamaIntentRules.hpp"

#include <regex>

namespace Slic3r { namespace GUI {
namespace OllamaIntentRules {

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
