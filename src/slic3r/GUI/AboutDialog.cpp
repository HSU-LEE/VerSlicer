#include "AboutDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Color.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/HyperLink.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "BambuSmartPrint/BambuSmartPrintUi.hpp"

#include <wx/clipbrd.h>

namespace Slic3r {
namespace GUI {

AboutDialogLogo::AboutDialogLogo(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    this->SetBackgroundColour(*wxWHITE);
    this->logo = ScalableBitmap(this, Slic3r::var("Verslicer_192px.png"), wxBITMAP_TYPE_PNG);
    this->SetMinSize(this->logo.GetBmpSize());

    this->Bind(wxEVT_PAINT, &AboutDialogLogo::onRepaint, this);
}

void AboutDialogLogo::onRepaint(wxEvent &event)
{
    wxPaintDC dc(this);
    dc.SetBackgroundMode(wxTRANSPARENT);

    wxSize size = this->GetSize();
    int logo_w = this->logo.GetBmpWidth();
    int logo_h = this->logo.GetBmpHeight();
    dc.DrawBitmap(this->logo.bmp(), (size.GetWidth() - logo_w)/2, (size.GetHeight() - logo_h)/2, true);

    event.Skip();
}


// -----------------------------------------
// CopyrightsDialog
// -----------------------------------------
CopyrightsDialog::CopyrightsDialog()
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, from_u8((boost::format("%1% - %2%")
        % (wxGetApp().is_editor() ? SLIC3R_APP_FULL_NAME : GCODEVIEWER_APP_NAME)
        % _utf8(L("Portions copyright"))).str()),
        wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    this->SetFont(wxGetApp().normal_font());
	this->SetBackgroundColour(*wxWHITE);

    wxStaticLine *staticline1 = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );

	auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add( staticline1, 0, wxEXPAND | wxALL, 5 );

    fill_entries();

    m_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition,
                              wxSize(40 * em_unit(), 20 * em_unit()), wxHW_SCROLLBAR_AUTO);
    m_html->SetMinSize(wxSize(FromDIP(870),FromDIP(520)));
    m_html->SetBackgroundColour(*wxWHITE);
    wxFont font = get_default_font(this);
    const int fs = font.GetPointSize();
    const int fs2 = static_cast<int>(1.2f*fs);
    int size[] = { fs, fs, fs, fs, fs2, fs2, fs2 };

    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
    m_html->SetBorders(2);
    m_html->SetPage(get_html_text());

    sizer->Add(m_html, 1, wxEXPAND | wxALL, 15);
    m_html->Bind(wxEVT_HTML_LINK_CLICKED, &CopyrightsDialog::onLinkClicked, this);

    SetSizer(sizer);
    sizer->SetSizeHints(this);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void CopyrightsDialog::fill_entries()
{
    m_entries = {
        { "Admesh",                                         "",      "https://admesh.readthedocs.io/" },
        { "Anti-Grain Geometry",                            "",      "http://antigrain.com" },
        { "ArcWelderLib",                                   "",      "https://plugins.octoprint.org/plugins/arc_welder" },
        { "Boost",                                          "",      "http://www.boost.org" },
        { "Cereal",                                         "",      "http://uscilab.github.io/cereal" },
        { "CGAL",                                           "",      "https://www.cgal.org" },
        { "Clipper",                                        "",      "http://www.angusj.co" },
        { "libcurl",                                        "",      "https://curl.se/libcurl" },
        { "Draco",                                          "",      "https://google.github.io/draco/" },
        { "Eigen3",                                         "",      "http://eigen.tuxfamily.org" },
        { "Expat",                                          "",      "http://www.libexpat.org" },
        { "fast_float",                                     "",      "https://github.com/fastfloat/fast_float" },
        { "GLAD (Multi-Language GL Loader-Generator)",       "",      "https://github.com/Dav1dde/glad" },
        { "GLFW",                                           "",      "https://www.glfw.org" },
        { "GNU gettext",                                    "",      "https://www.gnu.org/software/gettext" },
        { "ImGUI",                                          "",      "https://github.com/ocornut/imgui" },
        { "ImGuizmo",                                       "",      "https://github.com/CedricGuillemet/ImGuizmo" },
        { "Libigl",                                         "",      "https://libigl.github.io" },
        { "libnest2d",                                      "",      "https://github.com/tamasmeszaros/libnest2d" },
        { "lib_fts",                                        "",      "https://www.forrestthewoods.com" },
        { "Mesa 3D",                                        "",      "https://mesa3d.org" },
        { "Miniz",                                          "",      "https://github.com/richgel999/miniz" },
        { "Nanosvg",                                        "",      "https://github.com/memononen/nanosvg" },
        { "nlohmann/json",                                  "",      "https://json.nlohmann.me" },
        { "Qhull",                                          "",      "http://qhull.org" },
        { "Open Cascade",                                   "",      "https://www.opencascade.com" },
        { "OpenGL",                                         "",      "https://www.opengl.org" },
        { "PoEdit",                                         "",      "https://poedit.net" },
        { "PrusaSlicer",                                    "",      "https://www.prusa3d.com" },
        { "Real-Time DXT1/DXT5 C compression library",      "",      "https://github.com/Cyan4973/RygsDXTc" },
        { "SemVer",                                         "",      "https://semver.org" },
        { "Shinyprofiler",                                  "",      "https://code.google.com/p/shinyprofiler" },
        { "SuperSlicer",                                    "",      "https://github.com/supermerill/SuperSlicer" },
        { "TBB",                                            "",      "https://www.intel.cn/content/www/cn/zh/developer/tools/oneapi/onetbb.html" },
        { "wxWidgets",                                      "",      "https://www.wxwidgets.org" },
        { "zlib",                                           "",      "http://zlib.net" },

    };
}

wxString CopyrightsDialog::get_html_text()
{
    wxColour bgr_clr = wxGetApp().get_window_default_clr();//wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

    const auto text_clr = wxGetApp().get_label_clr_default();// wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    const auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));
    const auto bgr_clr_str = encode_color(ColorRGB(bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue()));

    const wxString copyright_str = _L("Copyright") + "&copy; ";

    wxString text = wxString::Format(
        "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
                "<font size=\"5\">%s</font><br/>"
                "<font size=\"5\">%s</font>"
                "<a href=\"%s\">%s.</a><br/>"
                "<font size=\"5\">%s.</font><br/>"
                "<br /><br />"
                "<font size=\"5\">%s</font><br/>"
                "<font size=\"5\">%s:</font><br/>"
                "<br />"
                "<font size=\"3\">",
         bgr_clr_str, text_clr_str, text_clr_str,
        _L("License"),
        _L("Verslicer is licensed under "),
        "https://www.gnu.org/licenses/agpl-3.0.html",_L("GNU Affero General Public License, version 3"),
        _L("Verslicer is based on PrusaSlicer and BambuStudio"),
        _L("Libraries"),
        _L("This software uses open source components whose copyright and other proprietary rights belong to their respective owners"));

    for (auto& entry : m_entries) {
        text += format_wxstr(
                    "%s<br/>"
                    , entry.lib_name);

         text += wxString::Format(
                    "<a href=\"%s\">%s</a><br/><br/>"
                    , entry.link, entry.link);
    }

    text += wxString(
                "</font>"
            "</font>"
            "</body>"
        "</html>");

    return text;
}

void CopyrightsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    const wxFont& font = GetFont();
    const int fs = font.GetPointSize();
    const int fs2 = static_cast<int>(1.2f*fs);
    int font_size[] = { fs, fs, fs, fs, fs2, fs2, fs2 };

    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CLOSE });

    const wxSize& size = wxSize(40 * em, 20 * em);

    m_html->SetMinSize(size);
    m_html->Refresh();

    SetMinSize(size);
    Fit();

    Refresh();
}

void CopyrightsDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

void CopyrightsDialog::onCloseDialog(wxEvent &)
{
     this->EndModal(wxID_CLOSE);
}

AboutDialog::AboutDialog()
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY,
                from_u8((boost::format(_utf8(L("About %s")))
                         % (wxGetApp().is_editor() ? SLIC3R_APP_FULL_NAME : GCODEVIEWER_APP_NAME))
                            .str()),
                wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    using namespace SlicePilotUi;

    SetFont(wxGetApp().normal_font());
    apply_dialog_chrome(this);

    const wxColour header_bg = StateColor::darkModeColorFor(wxColour(38, 46, 48));
    const wxColour body_bg   = StateColor::darkModeColorFor(wxColour(248, 248, 248));
    const wxColour title_fg  = StateColor::darkModeColorFor(wxColour(248, 250, 252));
    const wxColour muted_fg  = Theme::text_muted();
    const wxColour body_fg   = Theme::text();

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Hero: logo + product name / version
    auto* header_panel = new wxPanel(this, wxID_ANY);
    header_panel->SetBackgroundColour(header_bg);
    auto* header_row = new wxBoxSizer(wxHORIZONTAL);

    const bool is_dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    m_logo_bitmap     = ScalableBitmap(this, is_dark ? "Verslicer_about_dark" : "Verslicer_about", 108);
    m_logo            = new wxStaticBitmap(header_panel, wxID_ANY, m_logo_bitmap.bmp());
    header_row->Add(m_logo, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(24));

    auto* title_col = new wxBoxSizer(wxVERTICAL);
    title_col->AddStretchSpacer();

    auto* app_name = new wxStaticText(header_panel, wxID_ANY, SLIC3R_APP_FULL_NAME);
    wxFont name_font = Label::Head_24;
    app_name->SetFont(name_font);
    app_name->SetForegroundColour(title_fg);
    app_name->SetBackgroundColour(header_bg);
    title_col->Add(app_name, 0, wxALIGN_RIGHT);

    title_col->AddSpacer(FromDIP(4));

    auto* version = new wxStaticText(header_panel, wxID_ANY, SoftFever_VERSION);
    version->SetFont(Label::Head_18);
    version->SetForegroundColour(Theme::primary());
    version->SetBackgroundColour(header_bg);
    title_col->Add(version, 0, wxALIGN_RIGHT);

    title_col->AddSpacer(FromDIP(2));

    auto* build_label = new wxStaticText(
        header_panel, wxID_ANY,
        wxString::Format(_L("Build %s"), wxString::FromUTF8(GIT_COMMIT_HASH)));
    build_label->SetFont(Label::Body_11);
    build_label->SetForegroundColour(muted_fg);
    build_label->SetBackgroundColour(header_bg);
    title_col->Add(build_label, 0, wxALIGN_RIGHT);

    title_col->AddStretchSpacer();
    header_row->Add(title_col, 1, wxEXPAND | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(24));
    header_panel->SetSizer(header_row);
    header_panel->SetMinSize(wxSize(FromDIP(560), FromDIP(148)));
    main_sizer->Add(header_panel, 0, wxEXPAND);

    auto* divider = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    divider->SetBackgroundColour(Theme::border());
    main_sizer->Add(divider, 0, wxEXPAND);

    // Body copy
    auto* body_panel = new wxPanel(this, wxID_ANY);
    body_panel->SetBackgroundColour(body_bg);
    auto* body_sizer = new wxBoxSizer(wxVERTICAL);
    body_sizer->AddSpacer(FromDIP(20));

    auto add_body_line = [&](const wxString& text, const wxFont& font, const wxColour& fg) {
        auto* line = new wxStaticText(body_panel, wxID_ANY, text, wxDefaultPosition,
                                      wxSize(FromDIP(520), -1), wxALIGN_LEFT);
        line->SetFont(font);
        line->SetForegroundColour(fg);
        line->SetBackgroundColour(body_bg);
        line->Wrap(FromDIP(520));
        body_sizer->Add(line, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(20));
    };

    add_body_line(
        wxString::Format(
            _L("%s is a Bambu Lab–focused slicer with Smart Print assistance — suggested settings, failure learning, and streamlined slicing."),
            SLIC3R_APP_FULL_NAME),
        Label::Body_13, body_fg);
    add_body_line(_L("Smart Print keeps data on this PC unless you sign in for printer sync."),
                  Label::Body_12, muted_fg);

    body_panel->SetSizer(body_sizer);
    main_sizer->Add(body_panel, 0, wxEXPAND);
    main_sizer->AddSpacer(FromDIP(8));

    // Footer: copyright, links, license
    auto* footer_panel = new wxPanel(this, wxID_ANY);
    footer_panel->SetBackgroundColour(body_bg);
    auto* footer_row = new wxBoxSizer(wxHORIZONTAL);

    auto* footer_left = new wxBoxSizer(wxVERTICAL);
    auto* copyright = new wxStaticText(
        footer_panel, wxID_ANY,
        wxString::Format(_L("Copyright © 2026 %s"), SLIC3R_APP_FULL_NAME));
    copyright->SetFont(Label::Body_11);
    copyright->SetForegroundColour(muted_fg);
    copyright->SetBackgroundColour(body_bg);
    footer_left->Add(copyright, 0, wxBOTTOM, FromDIP(4));

    auto* links_row = new wxBoxSizer(wxHORIZONTAL);
    m_github_link   = new HyperLink(footer_panel, _L("GitHub"), "https://github.com/HSU-LEE/VerSlicer");
    m_github_link->SetFont(Label::Body_12);
    m_github_link->SetBackgroundColour(body_bg);
    links_row->Add(m_github_link, 0, wxALIGN_CENTER_VERTICAL);

    links_row->AddSpacer(FromDIP(12));
    auto* sep = new wxStaticText(footer_panel, wxID_ANY, wxS("·"));
    sep->SetForegroundColour(muted_fg);
    sep->SetBackgroundColour(body_bg);
    links_row->Add(sep, 0, wxALIGN_CENTER_VERTICAL);
    links_row->AddSpacer(FromDIP(12));

    auto* site_link = new HyperLink(footer_panel, _L("Releases"), "https://github.com/HSU-LEE/VerSlicer/releases");
    site_link->SetFont(Label::Body_12);
    site_link->SetBackgroundColour(body_bg);
    links_row->Add(site_link, 0, wxALIGN_CENTER_VERTICAL);

    footer_left->Add(links_row, 0);
    footer_row->Add(footer_left, 1, wxLEFT | wxBOTTOM, FromDIP(20));

    auto* license_btn = new Button(footer_panel, _L("License Info"));
    license_btn->SetStyle(ButtonStyle::Regular, ButtonType::Window);
    footer_row->Add(license_btn, 0, wxRIGHT | wxALIGN_BOTTOM, FromDIP(20));

    footer_panel->SetSizer(footer_row);
    main_sizer->Add(footer_panel, 0, wxEXPAND);
    main_sizer->AddSpacer(FromDIP(20));

    license_btn->Bind(wxEVT_BUTTON, &AboutDialog::onCopyrightBtn, this);

    wxGetApp().UpdateDlgDarkUI(this);
    SetSizer(main_sizer);
    Layout();
    Fit();
    CenterOnParent();
}

void AboutDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_logo_bitmap.msw_rescale();
    m_logo->SetBitmap(m_logo_bitmap.bmp());

    if (m_github_link)
        m_github_link->SetFont(Label::Body_12);

    const int& em = em_unit();
    msw_buttons_rescale(this, em, { wxID_CLOSE, m_copy_rights_btn_id });

    const wxSize& size = wxSize(65 * em, 30 * em);
    SetMinSize(size);
    Fit();
    Refresh();
}

void AboutDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

void AboutDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
}

void AboutDialog::onCopyrightBtn(wxEvent &)
{
    CopyrightsDialog dlg;
    dlg.ShowModal();
}

void AboutDialog::onCopyToClipboard(wxEvent&)
{
    wxTheClipboard->Open();
    wxTheClipboard->SetData(new wxTextDataObject(_L("Version") + " " + GUI_App::format_display_version()));
    wxTheClipboard->Close();
}

} // namespace GUI
} // namespace Slic3r
