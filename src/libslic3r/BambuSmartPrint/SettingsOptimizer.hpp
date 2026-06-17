#ifndef slic3r_SettingsOptimizer_hpp_
#define slic3r_SettingsOptimizer_hpp_

#include "BambuSmartPrintTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class SettingsOptimizer
{
public:
    static AutoSettingsResult optimize_from_diagnosis(const DynamicPrintConfig& current,
                                                      const FailureDiagnosis& diagnosis,
                                                      const PrinterLearningProfile* learning = nullptr,
                                                      bool conservative = false);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
