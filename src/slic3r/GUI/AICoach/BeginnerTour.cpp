#include "BeginnerTour.hpp"

#include "AICoachController.hpp"
#include "AIGuiOrchestrator.hpp"
#include "AICoachTypes.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kTourSection = "beginner_tour";
constexpr const char* kFirstSlice  = "first_slice_hint";

} // namespace

void BeginnerTour::on_first_slice(Plater* plater)
{
    (void) plater;
    if (!wxGetApp().app_config || wxGetApp().get_mode() != comSimple)
        return;
    if (wxGetApp().app_config->get(kTourSection, kFirstSlice) == "1")
        return;
    if (!AICoachController::is_enabled_for_current_mode())
        return;
    if (!AIGuiOrchestrator::instance().should_enqueue_beginner_tour())
        return;

    wxGetApp().app_config->set(kTourSection, kFirstSlice, "1");
    wxGetApp().app_config->save();

    AICoachCard c;
    c.trigger    = AICoachTriggerId::SliceDoneSend;
    c.importance = AICoachImportance::Low;
    c.body       = _u8L("Tip: open the Preview tab to see each layer. Support material is shown in a different color.");
    c.auto_dismiss_ms = 12000;
    c.buttons    = {
        {_u8L("Open Preview"), AICoachButtonRole::Primary, "preview_tab", {}},
        {_u8L("Close"), AICoachButtonRole::Secondary, "dismiss", {}},
    };
    AICoachController::instance().enqueue_card(std::move(c));
}

}} // namespace
