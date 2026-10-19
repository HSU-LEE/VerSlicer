#include "CrashReporter.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"

#include "GUI_App.hpp"
#include "I18N.hpp"

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <wx/msgdlg.h>
#include <wx/utils.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <sstream>

#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kIssueUrl = "https://github.com/HSU-LEE/VerSlicer/issues/new";

// Filled once by install(); the signal handler only reads these.
char g_report_path[1024] = {0};
char g_header[256]       = {0};
// The first writer wins: terminate() aborts, and SIGABRT must not overwrite its reason.
volatile std::sig_atomic_t g_written = 0;

void write_str(int fd, const char* s) { (void) !::write(fd, s, std::strlen(s)); }

// Async-signal-safe enough for a last gasp: open/write/backtrace_symbols_fd only.
void write_report(const char* reason)
{
    if (!g_report_path[0] || g_written)
        return;
    g_written = 1;
    const int fd = ::open(g_report_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    write_str(fd, g_header);
    write_str(fd, "reason: ");
    write_str(fd, reason);
    write_str(fd, "\nbacktrace:\n");
    void*     frames[64];
    const int n = ::backtrace(frames, 64);
    ::backtrace_symbols_fd(frames, n, fd);
    ::close(fd);
}

void on_signal(int sig)
{
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGBUS ? "SIGBUS"
                     : sig == SIGILL  ? "SIGILL"  : sig == SIGFPE  ? "SIGFPE"  : "signal";
    write_report(name);
    // Let the default handler run so macOS still records its own crash log.
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

std::terminate_handler g_prev_terminate = nullptr;

void on_terminate()
{
    std::string reason = "std::terminate";
    if (auto ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            reason += std::string(" (uncaught exception: ") + e.what() + ")";
        } catch (...) {
            reason += " (unknown exception)";
        }
    }
    write_report(reason.c_str());
    if (g_prev_terminate)
        g_prev_terminate();
    std::abort();
}

std::string report_path() { return (boost::filesystem::path(data_dir()) / "crash" / "last_crash.txt").string(); }

std::string url_encode(const std::string& s)
{
    std::ostringstream out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out << buf;
        }
    }
    return out.str();
}

} // namespace

void CrashReporter::install()
{
    const std::string path = report_path();
    boost::system::error_code ec;
    boost::filesystem::create_directories(boost::filesystem::path(path).parent_path(), ec);
    std::snprintf(g_report_path, sizeof(g_report_path), "%s", path.c_str());

    char when[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::snprintf(g_header, sizeof(g_header), "VerSlicer %s (%s)\nsession started: %s\n", SoftFever_VERSION,
                  wxGetOsDescription().utf8_string().c_str(), when);

    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE})
        std::signal(sig, on_signal);
    g_prev_terminate = std::set_terminate(on_terminate);
}

std::string CrashReporter::issue_body(const std::string& report, const std::string& home_dir, size_t max_chars)
{
    std::string scrubbed = report;
    if (!home_dir.empty())
        boost::replace_all(scrubbed, home_dir, "~");
    if (scrubbed.size() > max_chars)
        scrubbed = scrubbed.substr(0, max_chars) + "\n…(truncated; attach the full crash file)";
    return "**What were you doing when VerSlicer quit?**\n\n\n\n**Crash report**\n\n```\n" + scrubbed + "\n```\n";
}

void CrashReporter::offer_report_from_last_run(wxWindow* parent)
{
    const std::string path = report_path();
    if (!boost::filesystem::exists(path))
        return;

    std::string report;
    {
        boost::nowide::ifstream in(path, std::ios::binary);
        std::ostringstream      ss;
        ss << in.rdbuf();
        report = ss.str();
    }
    // Keep one copy for the user to attach, out of the way of the next crash.
    const std::string kept = path + ".reported";
    boost::system::error_code ec;
    boost::filesystem::rename(path, kept, ec);
    if (report.empty())
        return;

    wxMessageDialog dlg(parent,
                        _L("VerSlicer quit unexpectedly last time. Open a GitHub issue with the crash report? "
                           "You can review and edit it before submitting; nothing is sent automatically."),
                        _L("Report a crash"), wxYES_NO | wxYES_DEFAULT | wxICON_WARNING);
    dlg.SetYesNoLabels(_L("Open issue"), _L("Not now"));
    if (dlg.ShowModal() != wxID_YES)
        return;

    const std::string home  = wxGetHomeDir().utf8_string();
    const std::string title = std::string("Crash in VerSlicer ") + SoftFever_VERSION;
    wxLaunchDefaultBrowser(wxString::FromUTF8(std::string(kIssueUrl) + "?labels=crash&title=" + url_encode(title)
                                              + "&body=" + url_encode(issue_body(report, home))));
    wxLaunchDefaultApplication(wxString::FromUTF8(boost::filesystem::path(kept).parent_path().string()));
}

}} // namespace
