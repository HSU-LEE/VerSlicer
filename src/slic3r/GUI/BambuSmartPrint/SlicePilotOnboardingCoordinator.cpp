#include "SlicePilotOnboardingCoordinator.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "../NotificationManager.hpp"
#include "BambuSmartPrintService.hpp"
#include "FirstPrintExperience.hpp"
#include "SlicePilotNetworkSetup.hpp"
#include "SlicePilotOnboardingFunnel.hpp"
#include "SlicePilotSetupHub.hpp"
#include "PrintReadinessGate.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r { namespace GUI {

namespace {

static constexpr const char* kSection = "slicepilot_onboarding";
static constexpr const char* kStartupDone = "startup_coordinator_done";
static constexpr const char* kHubTipDone  = "setup_hub_tip_done";

} // namespace

void SlicePilotOnboardingCoordinator::initialize_defaults(Slic3r::AppConfig* cfg)
{
    if (!cfg)
        return;
    if (!cfg->has_section(kSection))
        cfg->set(kSection, kStartupDone, "0");
}

void SlicePilotOnboardingCoordinator::schedule_post_init()
{
    if (!wxGetApp().app_config)
        return;

    SlicePilotOnboardingFunnel::initialize_defaults(wxGetApp().app_config);
    SlicePilotOnboardingCoordinator::initialize_defaults(wxGetApp().app_config);

    wxGetApp().CallAfter([]() {
        if (!wxGetApp().app_config)
            return;
        if (wxGetApp().app_config->get(kSection, kStartupDone) == "1")
            return;

        wxGetApp().app_config->set(kSection, kStartupDone, "1");
        wxGetApp().app_config->save();

        // Mark plugin offer as handled — Setup Hub step 2 covers installation.
        static constexpr const char* kOffered = "slicepilot_network_plugin_offer_done";
        if (!PrintReadinessGate::network_plugin_ready()) {
            wxGetApp().app_config->set_bool(kOffered, true);
            wxGetApp().app_config->save();
        } else {
            SlicePilotNetworkSetup::maybe_offer_first_run_plugin_install();
        }

        // Defer empty-plate sample modal; Setup Hub step 4 or Print gate handles it.
        if (Plater* plater = wxGetApp().plater())
            SlicePilotSetupHub::refresh_all(plater);

        on_setup_hub_first_visit(wxGetApp().plater());
    });
}

void SlicePilotOnboardingCoordinator::on_guide_completed()
{
    SlicePilotOnboardingFunnel::record_wizard_completed();
    if (!wxGetApp().app_config)
        return;
    if (!wxGetApp().app_config->has("bambu_smart_print_auto_load_mode"))
        BambuSmartPrintService::set_auto_load_mode(BambuSmartPrintService::AutoLoadMode::FullDialog);

    wxGetApp().CallAfter([]() {
        Plater* plater = wxGetApp().plater();
        SlicePilotSetupHub::refresh_all(plater);
        on_setup_hub_first_visit(plater);
        BambuSmartPrintService::instance().refresh_all_panels();
    });
}

void SlicePilotOnboardingCoordinator::on_setup_hub_first_visit(Plater* plater)
{
    if (!plater || !wxGetApp().app_config)
        return;
    if (wxGetApp().app_config->get(kSection, kHubTipDone) == "1")
        return;
  if (SlicePilotSetupHub::completed_count() >= 4)
        return;

    wxGetApp().app_config->set(kSection, kHubTipDone, "1");
    wxGetApp().app_config->save();

    if (NotificationManager* nm = plater->get_notification_manager()) {
        nm->push_notification(
            NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::RegularNotificationLevel,
            std::string(_L("Complete the Setup steps on the Smart Print bar, then press Print.").utf8_str()));
    }
}

}} // namespace Slic3r::GUI
