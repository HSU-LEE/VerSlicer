#ifndef slic3r_BambuSmartPrint_ChangeEffects_hpp_
#define slic3r_BambuSmartPrint_ChangeEffects_hpp_

#include "BambuSmartPrintTypes.hpp"

#include <string>
#include <vector>

namespace Slic3r {
namespace BambuSmartPrint {

/** Rough direction of what a set of setting changes does to the print, so a
 *  review can say "longer, stronger, more filament" before slicing. Each
 *  score is the sum of per-setting nudges; only the sign and size matter. */
struct ChangeEffects
{
    int time{0};      // + longer print
    int strength{0};  // + stronger part
    int filament{0};  // + more material
    int surface{0};   // + smoother / finer finish
    int adhesion{0};  // + sticks to the bed better

    bool empty() const { return !time && !strength && !filament && !surface && !adhesion; }

    static ChangeEffects estimate(const std::vector<SettingChange>& changes);

    /** One line, e.g. "Print time: longer · Strength: stronger · Filament: more". */
    std::string summary() const;
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
