#ifndef slic3r_SlicePilotNetworkSetup_hpp_
#define slic3r_SlicePilotNetworkSetup_hpp_

namespace Slic3r { namespace GUI {

class SlicePilotNetworkSetup
{
public:
    /** Offer plugin download on first launch when networking is not available. */
    static void maybe_offer_first_run_plugin_install();
};

}} // namespace Slic3r::GUI

#endif
