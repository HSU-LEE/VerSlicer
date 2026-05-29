#include "SmartPrintOrchestrator.hpp"
#include "../Plater.hpp"
#include "../PartPlate.hpp"
#include "../GUI_App.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r { namespace GUI {

bool SmartPrintOrchestrator::has_open_plate(Plater* plater)
{
    return plater && plater->get_partplate_list().get_curr_plate();
}

DynamicPrintConfig SmartPrintOrchestrator::full_plate_config(Plater* plater, PresetBundle* bundle)
{
    DynamicPrintConfig cfg;
    if (!bundle) return cfg;
    cfg = bundle->full_config();
    if (plater) {
        if (PartPlate* plate = plater->get_partplate_list().get_curr_plate())
            cfg.apply(*plate->config());
    }
    return cfg;
}

bool SmartPrintOrchestrator::reslice(Plater* plater)
{
    if (!plater) return false;
    plater->reslice();
    return true;
}

bool SmartPrintOrchestrator::export_gcode(Plater* plater, const std::string& path)
{
    if (!plater)
        return false;
    return plater->export_gcode_to_path(path);
}

bool SmartPrintOrchestrator::select_filament_preset(PresetBundle* bundle, const std::string& name)
{
    if (!bundle || name.empty()) return false;
    if (bundle->filaments.find_preset(name) == nullptr) return false;
    bundle->filaments.select_preset_by_name(name, true);
    return true;
}

bool SmartPrintOrchestrator::select_printer_preset(PresetBundle* bundle, const std::string& name)
{
    if (!bundle || name.empty()) return false;
    if (bundle->printers.find_preset(name) == nullptr) return false;
    bundle->printers.select_preset_by_name(name, true);
    return true;
}

void SmartPrintOrchestrator::refresh_plater(Plater* plater)
{
    if (plater)
        plater->update();
}

bool SmartPrintOrchestrator::slice_all_plates(Plater* plater)
{
    if (!plater) return false;
    plater->reslice();
    return true;
}

}} // namespace
