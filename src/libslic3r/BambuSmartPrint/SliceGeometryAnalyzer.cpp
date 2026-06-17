#include "SliceGeometryAnalyzer.hpp"
#include "ConfigOptionRead.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

SliceAnalysis SliceGeometryAnalyzer::analyze(const Print& print, const DynamicPrintConfig& config)
{
    SliceAnalysis s;
    if (print.objects().empty())
        return s;

    const float layer_height = config.has("layer_height")
        ? config_get_float(config, "layer_height", 0.2f) : 0.2f;

    double total_extrusion_area = 0.0;
    double total_overhang_area  = 0.0;
    int    unsupported_islands  = 0;
    float  bridge_max           = 0.f;

    const double layer_mm = std::max(0.08, double(layer_height));
    const double island_side_mm = std::max(0.35, layer_mm * 2.5);
    const double island_area_thresh = scaled<double>(island_side_mm) * scaled<double>(island_side_mm);
    const double min_bridge_mm = std::max(1.5, layer_mm * 4.0);

    for (const PrintObject* object : print.objects()) {
        if (!object || object->layer_count() < 2)
            continue;

        const size_t layer_count = object->layer_count();
        const size_t layer_start = layer_count > 4 ? layer_count / 8 : 1;
        for (size_t lid = std::max(size_t(1), layer_start); lid < layer_count; ++lid) {
            const Layer* layer = object->get_layer(lid);
            const Layer* lower = object->get_layer(lid - 1);
            if (!layer || !lower)
                continue;

            const double layer_area = area(layer->lslices_extrudable);
            total_extrusion_area += layer_area;

            const ExPolygons overhang = diff_ex(layer->lslices_extrudable, lower->lslices);
            const double oh_area = area(overhang);
            total_overhang_area += oh_area;

            for (const ExPolygon& exp : overhang) {
                if (exp.area() > island_area_thresh)
                    ++unsupported_islands;
                const double span_mm = unscale<double>(sqrt(exp.area()));
                if (span_mm >= min_bridge_mm)
                    bridge_max = std::max(bridge_max, float(span_mm));
            }
        }
    }

    if (total_extrusion_area > 0.0)
        s.overhang_area_ratio = float(total_overhang_area / total_extrusion_area);
    s.unsupported_islands_count = unsupported_islands;
    s.bridge_length_max_mm      = bridge_max;
    s.valid                     = total_extrusion_area > 0.0;

    if (s.overhang_area_ratio > 0.22f)
        s.risk_notes.push_back("Large unsupported slice area detected");
    else if (s.overhang_area_ratio > 0.12f)
        s.risk_notes.push_back("Moderate unsupported slice area");
    if (unsupported_islands >= 8)
        s.risk_notes.push_back("Many unsupported islands in slice");
    else if (unsupported_islands >= 3)
        s.risk_notes.push_back("Several unsupported islands in slice");
    if (bridge_max > 18.f)
        s.risk_notes.push_back("Very long bridge spans in slice");
    else if (bridge_max > 12.f)
        s.risk_notes.push_back("Long bridge spans in slice");
    if (s.overhang_area_ratio > 0.08f && unsupported_islands == 0)
        s.risk_notes.push_back("Minor overhangs — verify cooling and support settings");
    if (layer_mm <= 0.12f && s.overhang_area_ratio > 0.15f)
        s.risk_notes.push_back("Fine layers with overhangs — consider slower outer walls");

    return s;
}

} // namespace BambuSmartPrint
} // namespace Slic3r
