#ifndef slic3r_SlicePilotSimpleLayout_hpp_
#define slic3r_SlicePilotSimpleLayout_hpp_

namespace Slic3r {
class AppConfig;
namespace GUI {

class MainFrame;

class SlicePilotSimpleLayout
{
public:
    static constexpr const char* kConfigKey = "slicepilot_simple_layout";

    static void initialize_defaults(Slic3r::AppConfig* cfg);
    static bool is_enabled();
    static void set_enabled(bool enabled);
    static void apply(MainFrame* frame);
};

}} // namespace Slic3r::GUI

#endif
