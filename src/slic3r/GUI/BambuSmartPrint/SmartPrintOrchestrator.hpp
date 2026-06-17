#ifndef slic3r_SmartPrintOrchestrator_hpp_
#define slic3r_SmartPrintOrchestrator_hpp_

#include "libslic3r/PrintConfig.hpp"
#include <string>

namespace Slic3r {
class Print;
class PresetBundle;

namespace GUI {

class Plater;

// Thin facade so Smart Print can drive standard Verslicer / Plater workflows.
class SmartPrintOrchestrator
{
public:
    static bool has_open_plate(Plater* plater);
    static DynamicPrintConfig full_plate_config(Plater* plater, PresetBundle* bundle);
    static bool reslice(Plater* plater);
    static bool export_gcode(Plater* plater, const std::string& path);
    static bool select_filament_preset(PresetBundle* bundle, const std::string& name);
    static bool select_printer_preset(PresetBundle* bundle, const std::string& name);
    static void refresh_plater(Plater* plater);
    static bool slice_all_plates(Plater* plater);
};

} // namespace GUI
} // namespace Slic3r

#endif
