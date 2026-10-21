#ifndef slic3r_BambuConnectUpdater_hpp_
#define slic3r_BambuConnectUpdater_hpp_

#include <functional>
#include <optional>
#include <string>

class wxWindow;

namespace Slic3r { namespace GUI { namespace BambuConnect {

/** One Bambu Connect build from Bambu Lab's update feed (electron-updater YAML). */
struct Release
{
    std::string version;   // e.g. "2.5.0-beta.15"
    std::string zip_url;   // absolute URL of the darwin-arm64 .zip
    std::string sha512;    // base64, as published in the feed
};

/** Parse the feed and pick the Apple Silicon .zip. Pure; nullopt if not found. */
std::optional<Release> parse_feed(const std::string& yaml, const std::string& base_url);

/** -1 / 0 / 1 for a < b, a == b, a > b ("2.5.0-beta.15" < "2.5.0" < "2.5.1-beta.1"). Pure. */
int compare_versions(const std::string& a, const std::string& b);

/** Launch the installed Bambu Connect by path (works right after an install,
 *  before Launch Services knows the app by name). False when missing. */
bool launch_installed();

/** Version of the installed Bambu Connect, empty when missing. Blocking; any thread. */
std::string installed_version();

/** Keep Bambu Connect up to date automatically (default on). */
bool auto_update_enabled();

/** Startup: when Bambu Connect is installed and auto-update is on, install a newer
 *  build in the background at most twice a day. Silent; never prompts. */
void check_for_updates_async();

/** Download, verify (checksum, Bambu Lab signature, notarization) and install the
 *  latest Bambu Connect, showing progress. `done(true)` once it is ready. GUI thread. */
void install_async(wxWindow* parent, std::function<void(bool ok)> done);

/** Ask once, then install. Used when a print needs Bambu Connect and it is missing. */
void offer_install(wxWindow* parent, std::function<void(bool ok)> done);

}}} // namespace Slic3r::GUI::BambuConnect

#endif
