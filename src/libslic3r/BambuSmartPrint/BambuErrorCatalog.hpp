#ifndef slic3r_BambuErrorCatalog_hpp_
#define slic3r_BambuErrorCatalog_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

struct ErrorCatalogEntry {
    FailureCategory category;
    const char*     title;
    const char*     description;
};

class BambuErrorCatalog
{
public:
    static void set_prefer_korean_ui(bool prefer_ko);
    static FailureDiagnosis diagnose(int mc_print_error_code, int print_error, const std::vector<std::string>& hms_codes);
    // Fills likely_causes and recommended_fixes for d.category (used after heuristic reclassification).
    static void apply_category_fixes(FailureDiagnosis& d);
    static void reload_user_catalog();
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif
