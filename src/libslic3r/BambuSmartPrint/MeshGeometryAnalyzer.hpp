#ifndef slic3r_MeshGeometryAnalyzer_hpp_
#define slic3r_MeshGeometryAnalyzer_hpp_

#include "BambuSmartPrintTypes.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class MeshGeometryAnalyzer
{
public:
    static ModelAnalysis analyze(const Model& model, const DynamicPrintConfig& config);
  // Objects on the active plate only (avoids analyzing the whole project on multi-plate beds).
    static ModelAnalysis analyze_objects(const std::vector<ModelObject*>& objects,
                                         const DynamicPrintConfig& config);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
