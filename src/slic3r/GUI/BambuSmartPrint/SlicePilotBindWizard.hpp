#ifndef slic3r_SlicePilotBindWizard_hpp_
#define slic3r_SlicePilotBindWizard_hpp_

namespace Slic3r { namespace GUI {

class Plater;

class SlicePilotBindWizard
{
public:
    /** Cloud login + device pick, or jump to Device tab. Returns true if a printer is bound. */
    static bool run(Plater* plater, wxWindow* parent = nullptr);
};

}} // namespace Slic3r::GUI

#endif
