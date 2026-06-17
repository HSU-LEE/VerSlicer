#ifndef slic3r_MakerWorldUrl_hpp_
#define slic3r_MakerWorldUrl_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** True if URL host is MakerWorld / MakerHub (any path). */
bool is_makerworld_host_url(const std::string& url);

/** True if URL is allowed for direct 3MF download (host + https). */
bool is_allowed_makerworld_download_url(const std::string& url);

/** Extract numeric design id from a MakerWorld model page URL, or empty. */
std::string parse_design_id_from_url(const std::string& url);

/** Extract profile id from #profileId-{id} URL fragment, or empty. */
std::string parse_profile_id_from_url(const std::string& url);

/** True when text looks like a MakerWorld model page or direct download link. */
bool text_contains_makerworld_link(const std::string& text);

/** Ensure browser-openable absolute MakerWorld URL (plugin may return api/... paths). */
std::string absolute_makerworld_browser_url(const std::string& url_or_path);

}} // namespace

#endif
