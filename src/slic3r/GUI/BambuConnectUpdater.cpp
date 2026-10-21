#include "BambuConnectUpdater.hpp"
#include "BambuConnect.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "UiDispatch.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <thread>
#include <vector>

#include <unistd.h>

namespace Slic3r { namespace GUI { namespace BambuConnect {

namespace fs = boost::filesystem;

namespace {

// Bambu Lab's own update feed for Bambu Connect (the channel its wiki download links use).
constexpr const char* kFeedBase = "https://public-cdn.bblmw.com/upgrade/bambu-connect/updates/";
constexpr const char* kFeedFile = "beta-mac.yml";
// Only accept builds signed by Bambu Lab's Developer ID team.
constexpr const char* kTeamId   = "T3UBR9Y3B2";
constexpr const char* kBundleId = "com.bambulab.bambu-connect";

constexpr const char* kAutoUpdateKey = "bambu_connect_auto_update";
constexpr const char* kLastCheckKey  = "bambu_connect_last_check";
constexpr const char* kAppName       = "Bambu Connect.app";

std::atomic<bool> g_busy{false};

std::string shell_quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    return out + "'";
}

/** Run a shell command; returns its exit code and stdout+stderr. Any thread. */
int run(const std::string& cmd, std::string* output = nullptr)
{
    FILE* p = ::popen((cmd + " 2>&1").c_str(), "r");
    if (!p)
        return -1;
    std::string out;
    char        buf[4096];
    while (size_t n = std::fread(buf, 1, sizeof(buf), p))
        out.append(buf, n);
    const int rc = ::pclose(p);
    if (output)
        *output = out;
    return rc;
}

fs::path installed_path()
{
    const fs::path sys = fs::path("/Applications") / kAppName;
    boost::system::error_code ec;
    if (fs::exists(sys, ec))
        return sys;
    const char* home = std::getenv("HOME");
    const fs::path user = fs::path(home ? home : "") / "Applications" / kAppName;
    if (fs::exists(user, ec))
        return user;
    return {};
}

/** Where a fresh install goes: /Applications when writable, else ~/Applications. */
fs::path install_target()
{
    if (::access("/Applications", W_OK) == 0)
        return fs::path("/Applications") / kAppName;
    const char* home = std::getenv("HOME");
    const fs::path dir = fs::path(home ? home : "") / "Applications";
    boost::system::error_code ec;
    fs::create_directories(dir, ec);
    return dir / kAppName;
}

bool is_running()
{
    return run("/usr/bin/pgrep -x 'Bambu Connect'") == 0;
}

std::optional<Release> fetch_latest(std::string* error)
{
    std::string body;
    if (run("/usr/bin/curl -sfL --max-time 30 " + shell_quote(std::string(kFeedBase) + kFeedFile), &body) != 0) {
        *error = "Could not reach Bambu Lab's update server.";
        return std::nullopt;
    }
    auto rel = parse_feed(body, kFeedBase);
    if (!rel)
        *error = "Bambu Lab's update feed did not list a build for this Mac.";
    return rel;
}

/** Download, verify and install `rel`. Worker thread. Returns empty on success. */
std::string install_release(const Release& rel)
{
    if (is_running())
        return "Bambu Connect is open; it will be updated next time.";
    const fs::path work = fs::path(data_dir()) / "bambu_connect_update";
    boost::system::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    const fs::path zip     = work / "bambu-connect.zip";
    const fs::path staging = work / "staging";
    std::string    out;

    if (run("/usr/bin/curl -sfL --max-time 1800 -o " + shell_quote(zip.string()) + " " + shell_quote(rel.zip_url), &out) != 0)
        return "Download failed.";

    std::string digest;
    run("/usr/bin/openssl dgst -sha512 -binary " + shell_quote(zip.string()) + " | /usr/bin/base64", &digest);
    boost::trim(digest);
    boost::erase_all(digest, "\n");
    if (digest != rel.sha512)
        return "The download did not match Bambu Lab's checksum.";

    fs::create_directories(staging, ec);
    if (run("/usr/bin/ditto -x -k " + shell_quote(zip.string()) + " " + shell_quote(staging.string()), &out) != 0)
        return "Could not unpack the download.";
    const fs::path fresh = staging / kAppName;
    if (!fs::exists(fresh, ec))
        return "The download did not contain Bambu Connect.";

    // Signed by Bambu Lab, intact, and notarized by Apple.
    if (run("/usr/bin/codesign --verify --deep --strict " + shell_quote(fresh.string()), &out) != 0)
        return "Bambu Connect's signature is not valid.";
    run("/usr/bin/codesign -dv --verbose=2 " + shell_quote(fresh.string()), &out);
    if (out.find(std::string("TeamIdentifier=") + kTeamId) == std::string::npos ||
        out.find(std::string("Identifier=") + kBundleId) == std::string::npos)
        return "Bambu Connect is not signed by Bambu Lab.";
    if (run("/usr/sbin/spctl -a -t exec " + shell_quote(fresh.string()), &out) != 0)
        return "macOS did not accept Bambu Connect (not notarized).";

    fs::path target = installed_path();
    if (target.empty())
        target = install_target();
    if (is_running())
        return "Bambu Connect is open; it will be updated next time.";

    const fs::path backup = work / "previous.app";
    const bool     had_old = fs::exists(target, ec);
    if (had_old) {
        fs::rename(target, backup, ec);
        if (ec)
            return "Could not replace the installed Bambu Connect.";
    }
    fs::rename(fresh, target, ec);
    if (ec) {
        // Different volume: copy instead.
        ec.clear();
        if (run("/usr/bin/ditto " + shell_quote(fresh.string()) + " " + shell_quote(target.string()), &out) != 0) {
            if (had_old)
                fs::rename(backup, target, ec);
            return "Could not install Bambu Connect.";
        }
    }
    // Register the app and its bambu-connect:// link handler right away, so
    // "Print with Bambu Connect" works without opening the app first.
    run("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f " +
        shell_quote(target.string()));
    fs::remove_all(work, ec);
    BOOST_LOG_TRIVIAL(info) << "Bambu Connect " << rel.version << " installed at " << target.string();
    return {};
}

/** Check the feed and install when missing or newer. Worker thread. */
std::string update_if_newer(std::string* version_out)
{
    std::string error;
    const auto  rel = fetch_latest(&error);
    if (!rel)
        return error;
    if (version_out)
        *version_out = rel->version;
    const std::string have = installed_version();
    if (!have.empty() && compare_versions(rel->version, have) <= 0)
        return {}; // already current
    return install_release(*rel);
}

} // namespace

std::optional<Release> parse_feed(const std::string& yaml, const std::string& base_url)
{
    Release rel;
    std::istringstream in(yaml);
    std::string line, pending_url;
    while (std::getline(in, line)) {
        std::string t = boost::trim_copy(line);
        if (boost::starts_with(t, "version:"))
            rel.version = boost::trim_copy(t.substr(8));
        else if (boost::starts_with(t, "- url:"))
            pending_url = boost::trim_copy(t.substr(6));
        else if (boost::starts_with(t, "sha512:") && !pending_url.empty()) {
            if (rel.zip_url.empty() && boost::ends_with(pending_url, "-darwin-arm64.zip")) {
                rel.zip_url = boost::starts_with(pending_url, "http") ? pending_url : base_url + pending_url;
                rel.sha512  = boost::trim_copy(t.substr(7));
            }
            pending_url.clear();
        }
    }
    boost::trim_if(rel.version, boost::is_any_of("'\""));
    if (rel.version.empty() || rel.zip_url.empty() || rel.sha512.empty())
        return std::nullopt;
    return rel;
}

int compare_versions(const std::string& a, const std::string& b)
{
    auto split = [](const std::string& v, std::vector<long>& nums, std::string& pre, long& pre_num) {
        const auto dash = v.find('-');
        const std::string core = v.substr(0, dash);
        pre = dash == std::string::npos ? "" : v.substr(dash + 1);
        std::vector<std::string> parts;
        boost::split(parts, core, boost::is_any_of("."));
        for (const auto& p : parts)
            nums.push_back(std::strtol(p.c_str(), nullptr, 10));
        while (nums.size() < 3)
            nums.push_back(0);
        const auto dot = pre.find_last_of('.');
        pre_num = dot == std::string::npos ? 0 : std::strtol(pre.c_str() + dot + 1, nullptr, 10);
        if (dot != std::string::npos)
            pre = pre.substr(0, dot);
    };
    std::vector<long> na, nb;
    std::string       pa, pb;
    long              qa = 0, qb = 0;
    split(a, na, pa, qa);
    split(b, nb, pb, qb);
    for (size_t i = 0; i < std::max(na.size(), nb.size()); ++i) {
        const long x = i < na.size() ? na[i] : 0, y = i < nb.size() ? nb[i] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    // A release ranks above its pre-releases.
    if (pa.empty() != pb.empty())
        return pa.empty() ? 1 : -1;
    if (pa != pb)
        return pa < pb ? -1 : 1;
    if (qa != qb)
        return qa < qb ? -1 : 1;
    return 0;
}

bool launch_installed()
{
    const fs::path app = installed_path();
    if (app.empty())
        return false;
    return wxExecute(wxString::Format("/usr/bin/open %s", wxString::FromUTF8(shell_quote(app.string()))), wxEXEC_ASYNC) != 0;
}

std::string installed_version()
{
    const fs::path app = installed_path();
    if (app.empty())
        return {};
    std::string out;
    if (run("/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' " +
                shell_quote((app / "Contents" / "Info.plist").string()),
            &out) != 0)
        return {};
    return boost::trim_copy(out);
}

bool auto_update_enabled()
{
    AppConfig* cfg = wxGetApp().app_config;
    return !cfg || !cfg->has(kAutoUpdateKey) || cfg->get_bool(kAutoUpdateKey);
}

void check_for_updates_async()
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg || !auto_update_enabled() || !installed() || g_busy.exchange(true))
        return;
    const long long now  = static_cast<long long>(std::time(nullptr));
    long long       last = 0;
    try {
        last = std::stoll(cfg->get(kLastCheckKey));
    } catch (...) {
    }
    if (now - last < 12 * 3600) {
        g_busy = false;
        return;
    }
    cfg->set(kLastCheckKey, std::to_string(now));
    std::thread([]() {
        std::string version;
        const std::string error = update_if_newer(&version);
        if (!error.empty())
            BOOST_LOG_TRIVIAL(warning) << "Bambu Connect auto-update: " << error;
        g_busy = false;
    }).detach();
}

void install_async(wxWindow* parent, std::function<void(bool ok)> done)
{
    if (g_busy.exchange(true)) {
        show_info(parent, _L("Bambu Connect is already being installed. Try again in a moment."), _L("Bambu Connect"));
        if (done)
            done(false);
        return;
    }
    auto* dlg = new wxProgressDialog(_L("Installing Bambu Connect"),
                                     _L("Downloading Bambu Connect from Bambu Lab (about 140 MB)…"), 100, parent,
                                     wxPD_APP_MODAL | wxPD_SMOOTH);
    auto* pulse = new wxTimer();
    pulse->Bind(wxEVT_TIMER, [dlg](wxTimerEvent&) { dlg->Pulse(); });
    pulse->Start(120);

    std::thread([parent, dlg, pulse, done]() {
        std::string       version;
        const std::string error = update_if_newer(&version);
        post_to_ui([parent, dlg, pulse, done, error, version]() {
            g_busy = false;
            pulse->Stop();
            delete pulse;
            dlg->Destroy();
            if (wxGetApp().is_closing())
                return;
            const bool ok = error.empty() && installed();
            if (!ok) {
                wxMessageDialog fail(parent,
                                     wxString::Format(_L("Bambu Connect could not be installed: %s\n\nYou can download it from Bambu Lab's website instead."),
                                                      wxString::FromUTF8(error)),
                                     _L("Bambu Connect"), wxYES_NO | wxYES_DEFAULT | wxICON_WARNING);
                fail.SetYesNoLabels(_L("Open download page"), _L("Cancel"));
                if (fail.ShowModal() == wxID_YES)
                    wxLaunchDefaultBrowser(kDownloadUrl);
            }
            if (done)
                done(ok);
        });
    }).detach();
}

void offer_install(wxWindow* parent, std::function<void(bool ok)> done)
{
    wxMessageDialog dlg(parent,
                        _L("Bambu Lab printers with the latest firmware only accept print jobs from other slicers through "
                           "Bambu Connect, Bambu Lab's free helper app. Developer Mode is not needed.\n\n"
                           "VerSlicer can install Bambu Connect for you (about 140 MB, from Bambu Lab) and keep it up to date "
                           "automatically. After installing, sign in to Bambu Connect with your Bambu Lab account."),
                        _L("Bambu Connect is needed to print"), wxYES_NO | wxYES_DEFAULT | wxICON_INFORMATION);
    dlg.SetYesNoLabels(_L("Install Bambu Connect"), _L("Cancel"));
    if (dlg.ShowModal() != wxID_YES) {
        if (done)
            done(false);
        return;
    }
    if (AppConfig* cfg = wxGetApp().app_config) {
        if (!cfg->has(kAutoUpdateKey))
            cfg->set_bool(kAutoUpdateKey, true);
    }
    install_async(parent, std::move(done));
}

}}} // namespace Slic3r::GUI::BambuConnect
