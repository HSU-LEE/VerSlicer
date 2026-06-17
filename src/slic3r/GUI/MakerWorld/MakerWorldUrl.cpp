#include "MakerWorldUrl.hpp"

#include "../GUI_App.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>

namespace Slic3r { namespace GUI {

namespace {

bool host_matches(const std::string& url)
{
    static const boost::regex re(
        R"((https?)://([^/?#]+))",
        boost::regex_constants::icase);
    boost::smatch m;
    if (!boost::regex_search(url, m, re) || m.size() < 3)
        return false;
    std::string host = m[2].str();
    boost::algorithm::to_lower(host);
    return host.find("makerworld.com") != std::string::npos
        || host.find("makerhub") != std::string::npos
        || host.find("bambu-lab.com") != std::string::npos;
}

} // namespace

bool is_makerworld_host_url(const std::string& url)
{
    return !url.empty() && host_matches(url);
}

bool is_allowed_makerworld_download_url(const std::string& url)
{
    if (!is_makerworld_host_url(url))
        return false;
    std::string lower = url;
    boost::algorithm::to_lower(lower);
    return lower.find("https://") == 0 || lower.find("http://") == 0;
}

std::string parse_design_id_from_url(const std::string& url)
{
    if (url.empty())
        return {};
    static const boost::regex id_re(R"(/models?/(\d+)(?:[-/?#]|$))", boost::regex_constants::icase);
    boost::smatch m;
    if (boost::regex_search(url, m, id_re) && m.size() > 1)
        return m[1].str();
    return {};
}

std::string parse_profile_id_from_url(const std::string& url)
{
    if (url.empty())
        return {};
    static const boost::regex profile_re(R"(#profileId-(\d+))", boost::regex_constants::icase);
    boost::smatch m;
    if (boost::regex_search(url, m, profile_re) && m.size() > 1)
        return m[1].str();
    return {};
}

std::string absolute_makerworld_browser_url(const std::string& url_or_path)
{
    if (url_or_path.empty())
        return {};
    if (url_or_path.find("https://") == 0 || url_or_path.find("http://") == 0)
        return url_or_path;

    const std::string host = wxGetApp().get_model_http_url(
        wxGetApp().app_config ? wxGetApp().app_config->get_country_code() : "US");
    if (!url_or_path.empty() && url_or_path.front() == '/')
        return host.substr(0, host.size() - 1) + url_or_path;
    return host + url_or_path;
}

bool text_contains_makerworld_link(const std::string& text)
{
    static const boost::regex link_re(
        R"((https?://[^\s]+))",
        boost::regex_constants::icase);
    boost::sregex_iterator it(text.begin(), text.end(), link_re);
    boost::sregex_iterator end;
    for (; it != end; ++it) {
        if (is_makerworld_host_url((*it)[1].str()))
            return true;
    }
    return false;
}

}} // namespace
