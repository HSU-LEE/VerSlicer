#ifndef slic3r_FailureAnalyzer_hpp_
#define slic3r_FailureAnalyzer_hpp_

#include "BambuSmartPrintTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class FailureAnalyzer
{
public:
    static FailureDiagnosis analyze_record(const PrintFailureRecord& record);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
