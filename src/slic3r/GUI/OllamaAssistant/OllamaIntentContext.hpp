#ifndef slic3r_OllamaIntentContext_hpp_
#define slic3r_OllamaIntentContext_hpp_

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace Slic3r { namespace GUI {

struct OllamaSelectionFootprint
{
    double x_mm{0.0};
    double y_mm{0.0};
    double z_mm{0.0};
    bool   valid{false};
};

/** Footprint-based brim width (mm); testable without GUI. */
inline double ollama_recommended_brim_width_mm(double footprint_x_mm, double footprint_y_mm)
{
    if (footprint_x_mm <= 0.0 || footprint_y_mm <= 0.0)
        return 5.0;
    const double area = footprint_x_mm * footprint_y_mm;
    if (area < 400.0)
        return 8.0;
    if (area <= 2500.0)
        return 5.0;
    return 3.0;
}

/** Tall/narrow heuristic: z / max(x,y) >= ratio. */
inline bool ollama_selection_is_tall_narrow(double x_mm, double y_mm, double z_mm, double ratio = 3.0)
{
    if (x_mm <= 0.0 || y_mm <= 0.0 || z_mm <= 0.0)
        return false;
    const double base = std::max(x_mm, y_mm);
    return base > 0.0 && (z_mm / base) >= ratio;
}

class OllamaIntentContext
{
public:
    static constexpr double kOverhangSupportRatio = 0.15;
    static constexpr int    kMaxFilamentIndex     = 15;

    static OllamaSelectionFootprint current_selection_footprint();
    static bool                     selection_is_tall_narrow(double ratio = 3.0);
    static bool                     slice_suggests_support(double min_overhang_ratio = kOverhangSupportRatio);
    static int                      slice_unsupported_islands();
    static bool                     print_support_enabled();
    static double                   recommended_brim_width_mm();

    static bool user_vague_fix_request(const std::string& user);
    static bool user_wants_warp_relief(const std::string& user);
    static bool user_wants_stringing_relief(const std::string& user);
    static bool user_wants_top_surface_quality(const std::string& user);
    static bool user_wants_first_layer_help(const std::string& user);
    static bool user_wants_lay_flat(const std::string& user);

    static nlohmann::json build_intent_signals_json();
    static void           refresh_cached_intent_signals();
    static nlohmann::json cached_intent_signals_json();

    static void mark_pending_slice_feedback();
    static void consume_slice_feedback_if_ready();

    static int clamp_filament_index(int idx, int max_exclusive)
    {
        if (idx < 0)
            return 0;
        if (max_exclusive <= 0)
            return 0;
        if (idx >= max_exclusive)
            return max_exclusive - 1;
        return idx;
    }

    static void refine_set_config_options(nlohmann::json& options, const std::string& user_request);
};

}} // namespace

#endif
