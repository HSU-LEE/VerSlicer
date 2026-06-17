#ifndef slic3r_SliceGeometryAnalyzer_hpp_
#define slic3r_SliceGeometryAnalyzer_hpp_

#include "BambuSmartPrintTypes.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

class Print;

namespace BambuSmartPrint {

class SliceGeometryAnalyzer
{
public:
    static SliceAnalysis analyze(const Print& print, const DynamicPrintConfig& config);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
