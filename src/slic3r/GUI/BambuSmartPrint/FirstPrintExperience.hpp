#ifndef slic3r_FirstPrintExperience_hpp_
#define slic3r_FirstPrintExperience_hpp_

#include <string>

namespace Slic3r {
class AppConfig;

namespace GUI {

class Plater;

// Optional sample model helpers for the Prepare bar (no UI restrictions or graduation).
class FirstPrintExperience
{
public:
    static void initialize_defaults(Slic3r::AppConfig* cfg);

    static int  local_successful_prints();
    static void record_successful_print();

    static std::string first_print_sample_model_path();
    static bool open_first_print_sample(Plater* plater);

    static void apply_bed_fit_fix(Plater* plater);

    /** One-time hint when the plate is empty on first launch. */
    static void maybe_suggest_sample_on_empty_plate(Plater* plater);
};

} // namespace GUI
} // namespace Slic3r

#endif
