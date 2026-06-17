#ifndef slic3r_SmartPrintReportExporter_hpp_
#define slic3r_SmartPrintReportExporter_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

class SmartPrintReportExporter
{
public:
    static std::string build_report_json(const ModelAnalysis& mesh,
                                         const ReadinessReport& readiness,
                                         const AutoSettingsResult& settings,
                                         const SuccessPrediction& prediction,
                                         int plate_index = -1,
                                         const PlateBatchSummary* batch = nullptr,
                                         const SliceAnalysis* slice = nullptr);

    static bool write_report_file(const std::string& path,
                                  const ModelAnalysis& mesh,
                                  const ReadinessReport& readiness,
                                  const AutoSettingsResult& settings,
                                  const SuccessPrediction& prediction,
                                  int plate_index = -1,
                                  const PlateBatchSummary* batch = nullptr,
                                  std::string* error_out = nullptr,
                                  const SliceAnalysis* slice = nullptr);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
