#include "BambuConnect.hpp"

#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Utils.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/msgdlg.h>
#include <wx/utils.h>

#include <cctype>
#include <cstdio>

namespace Slic3r { namespace GUI { namespace BambuConnect {

std::string encode_uri_component(const std::string& s)
{
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string import_url(const std::string& absolute_path, const std::string& display_name)
{
    return "bambu-connect://import-file?path=" + encode_uri_component(absolute_path) + "&name=" +
           encode_uri_component(display_name) + "&version=1.0.0";
}

bool installed()
{
    namespace fs = boost::filesystem;
    boost::system::error_code ec;
    if (fs::exists("/Applications/Bambu Connect.app", ec))
        return true;
    const wxString home = wxGetHomeDir();
    return fs::exists(fs::path(home.utf8_string()) / "Applications" / "Bambu Connect.app", ec);
}

namespace {

std::string safe_file_stem(std::string name)
{
    for (char& c : name)
        if (c == '/' || c == ':' || c == '\\')
            c = '_';
    if (name.empty())
        name = "plate";
    return name;
}

void offer_download(wxWindow* parent)
{
    wxMessageDialog dlg(parent,
                        _L("Bambu Lab printers with the latest firmware only accept print jobs from other slicers through "
                           "Bambu Connect, Bambu Lab's free helper app. Developer Mode is not needed.\n\n"
                           "Install Bambu Connect (macOS 13 or later), sign in with your Bambu Lab account, then press Print again."),
                        _L("Bambu Connect is needed to print"), wxYES_NO | wxYES_DEFAULT | wxICON_INFORMATION);
    dlg.SetYesNoLabels(_L("Download Bambu Connect"), _L("Cancel"));
    if (dlg.ShowModal() == wxID_YES)
        wxLaunchDefaultBrowser(kDownloadUrl);
}

} // namespace

bool send_current_plate(Plater* plater)
{
    if (!plater)
        return false;
    wxWindow* parent = plater;

    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    if (!plate || !plate->is_slice_result_ready_for_export()) {
        show_info(parent, _L("Slice the plate first, then press Print again."), _L("Print with Bambu Connect"));
        return false;
    }
    if (!installed()) {
        offer_download(parent);
        return false;
    }

    namespace fs = boost::filesystem;
    const fs::path dir = fs::path(data_dir()) / "bambu_connect";
    boost::system::error_code ec;
    fs::create_directories(dir, ec);

    std::string name = plater->get_project_name().utf8_string();
    const int   plate_idx = plater->get_partplate_list().get_curr_plate_index();
    if (plater->get_partplate_list().get_plate_count() > 1)
        name += " - plate " + std::to_string(plate_idx + 1);
    const fs::path out = dir / (safe_file_stem(name) + ".gcode.3mf");

    const int rc = plater->export_3mf(out, SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::WithGcode |
                                               SaveStrategy::SkipModel,
                                      plate_idx);
    if (rc < 0 || !fs::exists(out, ec)) {
        BOOST_LOG_TRIVIAL(error) << "Bambu Connect: export failed, rc=" << rc << " path=" << out.string();
        show_error(parent, _L("Could not prepare the sliced file for Bambu Connect."));
        return false;
    }

    const std::string url = import_url(fs::absolute(out).string(), name);
    BOOST_LOG_TRIVIAL(info) << "Bambu Connect: opening " << url;
    if (!wxLaunchDefaultBrowser(wxString::FromUTF8(url))) {
        offer_download(parent);
        return false;
    }
    return true;
}

void open_app()
{
    if (installed())
        wxExecute("open -a \"Bambu Connect\"", wxEXEC_ASYNC);
    else
        wxLaunchDefaultBrowser(kDownloadUrl);
}

}}} // namespace Slic3r::GUI::BambuConnect
