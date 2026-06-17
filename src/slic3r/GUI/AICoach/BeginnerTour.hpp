#ifndef slic3r_BeginnerTour_hpp_
#define slic3r_BeginnerTour_hpp_

namespace Slic3r { namespace GUI {

class Plater;

/** One-shot hints after milestones (no modal). */
class BeginnerTour
{
public:
    static void on_first_slice(Plater* plater);
};

}} // namespace

#endif
