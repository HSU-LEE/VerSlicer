#ifndef slic3r_SlicePilotOnboardingCoordinator_hpp_
#define slic3r_SlicePilotOnboardingCoordinator_hpp_

namespace Slic3r {
class AppConfig;

namespace GUI {

class Plater;

/** Coordinates first-run modals: at most one startup dialog; defers plugin/sample hints to Setup Hub. */
class SlicePilotOnboardingCoordinator
{
public:
    static void initialize_defaults(Slic3r::AppConfig* cfg);
    static void schedule_post_init();
    static void on_guide_completed();
    static void on_setup_hub_first_visit(Plater* plater);
};

}} // namespace GUI, Slic3r

#endif