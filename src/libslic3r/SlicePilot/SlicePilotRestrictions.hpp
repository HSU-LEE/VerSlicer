#ifndef slic3r_SlicePilotRestrictions_hpp_
#define slic3r_SlicePilotRestrictions_hpp_

#include <string>

namespace Slic3r {

class Preset;
class PresetBundle;

namespace SlicePilot {

// SlicePilot: Bambu Lab (BBL) printers only.
static constexpr const char* VENDOR_BBL              = "BBL";
static constexpr const char* VENDOR_ORCA_FILAMENT_LIB = "OrcaFilamentLibrary";

bool is_bbl_printer_preset(const Preset& preset, const PresetBundle* bundle = nullptr);
bool is_vendor_allowed_for_slicepilot(const std::string& vendor_name);

void enforce_bbl_only_bundle(PresetBundle& bundle);

// True when the active (edited) printer preset is a Bambu Lab profile.
bool is_active_printer_bbl(const PresetBundle& bundle);

} // namespace SlicePilot
} // namespace Slic3r

#endif
