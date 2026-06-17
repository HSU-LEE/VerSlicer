#ifndef slic3r_BambuSmartPrintJson_hpp_
#define slic3r_BambuSmartPrintJson_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <nlohmann/json_fwd.hpp>

namespace Slic3r {
namespace BambuSmartPrint {

nlohmann::json diagnosis_to_json(const FailureDiagnosis& d);
FailureDiagnosis diagnosis_from_json(const nlohmann::json& j);

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
