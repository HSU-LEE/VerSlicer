#ifndef slic3r_PrintErrorUi_hpp_
#define slic3r_PrintErrorUi_hpp_

#include "bambu_networking.hpp"

#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/window.h>

#include <algorithm>

namespace Slic3r {
namespace GUI {

// Show the Bambu cloud server status link only for cloud/server-side failures.
inline bool should_show_bambu_server_status_link(int code)
{
    if (code == BAMBU_NETWORK_ERR_CONNECTION_TO_SERVER_FAILED)
        return true;

    if (code == BAMBU_NETWORK_ERR_CONNECTION_TO_PRINTER_FAILED
        || code == BAMBU_NETWORK_ERR_CONNECT_FAILED
        || code == BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED
        || code == BAMBU_NETWORK_ERR_PRINT_WR_UPLOAD_FTP_FAILED
        || code == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED
        || code == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED
        || code == BAMBU_NETWORK_SIGNED_ERROR
        || code == BAMBU_NETWORK_ERR_INVALID_HANDLE
        || code == BAMBU_NETWORK_ERR_CANCELED)
        return false;

    const bool cloud_wr = code <= BAMBU_NETWORK_ERR_PRINT_WR_GET_USER_UPLOAD_FAILED
        && code >= BAMBU_NETWORK_ERR_PRINT_WR_REQUEST_PROJECT_ID_FAILED
        && code != BAMBU_NETWORK_ERR_PRINT_WR_UPLOAD_FTP_FAILED;
    const bool cloud_sp = code <= BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED
        && code >= BAMBU_NETWORK_ERR_PRINT_SP_REQUEST_PROJECT_ID_FAILED
        && code != BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED;

    if (cloud_wr || cloud_sp)
        return true;

    return code == BAMBU_NETWORK_ERR_TIMEOUT
        || code == BAMBU_NETWORK_ERR_CHECK_MD5_FAILED
        || code == BAMBU_NETWORK_ERR_GET_USER_PRINTINFO_FAILED
        || code == BAMBU_NETWORK_ERR_FILE_OVER_SIZE;
}

inline void update_bambu_server_status_link_visibility(wxWindow* link, int code)
{
    if (!link)
        return;
    if (should_show_bambu_server_status_link(code))
        link->Show();
    else
        link->Hide();
}

inline void layout_print_failed_scrolled_panel(wxScrolledWindow* panel, int max_height_dip = 200)
{
    if (!panel)
        return;
    wxSizer* sizer = panel->GetSizer();
    if (!sizer)
        return;

    panel->Layout();
    sizer->Layout();
    const wxSize content_min = sizer->GetMinSize();
    const int panel_w = panel->GetMinSize().GetWidth() > 0 ? panel->GetMinSize().GetWidth() : content_min.x;
    const int max_h     = panel->FromDIP(max_height_dip);
    const int h         = std::min(content_min.y, max_h);
    panel->SetMinSize(wxSize(panel_w, h));
    panel->SetMaxSize(wxSize(panel_w, max_h));
    panel->SetVirtualSize(content_min);
    panel->FitInside();
    panel->Layout();
}

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PrintErrorUi_hpp_
