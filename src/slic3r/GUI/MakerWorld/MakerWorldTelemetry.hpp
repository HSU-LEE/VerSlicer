#ifndef slic3r_MakerWorldTelemetry_hpp_
#define slic3r_MakerWorldTelemetry_hpp_

#include <string>

namespace Slic3r { namespace GUI {

struct MakerWorldTelemetry
{
    static void search_started(const std::string& query);
    static void search_finished(int count, int latency_ms, const std::string& source, bool ok = true,
                                const std::string& detail = {});
    static void import_started(const std::string& design_id);
    static void import_finished(bool ok, const std::string& design_id, const std::string& detail = {});

    /** Called from Plater when import_model_id completes or fails. */
    static void plater_import_done(bool ok, const std::string& detail = {});

    static void translation_failed(const std::string& query, bool fallback_used = true);
};

}} // namespace

#endif
