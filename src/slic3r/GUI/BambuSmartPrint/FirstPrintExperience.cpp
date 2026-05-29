#include "FirstPrintExperience.hpp"

#include "../GUI_App.hpp"
#include "../GUI_Utils.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "../GUI_Utils.hpp"
#include "../I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/filesystem.hpp>
#include <wx/msgdlg.h>

namespace Slic3r { namespace GUI {

namespace {

static const char* kSection    = "first_print";
static const char* kSuccessKey = "successful_prints";
static const char* kSampleHintKey = "empty_plate_sample_hint_shown";

static AppConfig* app_cfg() { return wxGetApp().app_config; }

static int cfg_int(const char* key, int default_val)
{
    if (!app_cfg() || !app_cfg()->has_section(kSection) || !app_cfg()->has(kSection, key))
        return default_val;
    try {
        return std::stoi(app_cfg()->get(kSection, key));
    } catch (...) {
        return default_val;
    }
}

static void cfg_set_int(const char* key, int value)
{
    if (!app_cfg())
        return;
    app_cfg()->set(kSection, key, std::to_string(value));
    app_cfg()->save();
}

} // namespace

void FirstPrintExperience::initialize_defaults(Slic3r::AppConfig* cfg)
{
    if (!cfg)
        return;
    if (!cfg->has_section(kSection))
        cfg->set(kSection, kSuccessKey, "0");
    else if (!cfg->has(kSection, kSuccessKey))
        cfg->set(kSection, kSuccessKey, "0");
}

int FirstPrintExperience::local_successful_prints()
{
    return cfg_int(kSuccessKey, 0);
}

void FirstPrintExperience::record_successful_print()
{
    cfg_set_int(kSuccessKey, local_successful_prints() + 1);
}

std::string FirstPrintExperience::first_print_sample_model_path()
{
    return (boost::filesystem::path(Slic3r::resources_dir()) / "models" / "first_print" / "starter_cube.stl").string();
}

bool FirstPrintExperience::open_first_print_sample(Plater* plater)
{
    if (!plater)
        return false;
    const std::string path = first_print_sample_model_path();
    if (!boost::filesystem::exists(path)) {
        show_error(plater, wxString::Format(_L("Sample model not found:\n%s"), wxString::FromUTF8(path)));
        return false;
    }
    const std::vector<boost::filesystem::path> paths{ boost::filesystem::path(path) };
    plater->load_files(paths);
    plater->arrange();
    return true;
}

void FirstPrintExperience::apply_bed_fit_fix(Plater* plater)
{
    if (!plater)
        return;
    plater->scale_selection_to_fit_print_volume();
    plater->arrange();
    plater->update();
}

void FirstPrintExperience::maybe_suggest_sample_on_empty_plate(Plater* plater)
{
    if (!plater || !app_cfg())
        return;
    if (app_cfg()->has(kSection, kSampleHintKey))
        return;
    if (!plater->model().objects.empty())
        return;

    app_cfg()->set(kSection, kSampleHintKey, "1");
    app_cfg()->save();

    wxMessageDialog dlg(plater,
        _L("Press Print to start Smart Print.\n\n"
           "Load the bundled sample cube now, or import your own model."),
        _L("First print"), wxYES_NO | wxICON_INFORMATION);
    dlg.SetYesNoLabels(_L("Load sample"), _L("Not now"));
    if (dlg.ShowModal() == wxID_YES)
        open_first_print_sample(plater);
}

}} // namespace Slic3r::GUI
