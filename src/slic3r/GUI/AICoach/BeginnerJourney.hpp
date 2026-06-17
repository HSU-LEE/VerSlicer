#ifndef slic3r_BeginnerJourney_hpp_
#define slic3r_BeginnerJourney_hpp_

namespace Slic3r { namespace GUI {

class GLCanvas3D;
class Plater;

/** Setup journey checklist (printer → model → slice → send). */
class BeginnerJourney
{
public:
    static void on_printer_configured();
    static void on_model_on_bed();
    static void on_sliced();
    static void on_sent_or_exported();

    static void render(GLCanvas3D& canvas);
    static bool update(int64_t delta_ms);
    static int completed_step_count();

private:
    static void save_flag(const char* key, bool done);
    static bool read_flag(const char* key);
};

}} // namespace

#endif
