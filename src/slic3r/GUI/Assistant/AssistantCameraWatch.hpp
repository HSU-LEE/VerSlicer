#ifndef slic3r_AssistantCameraWatch_hpp_
#define slic3r_AssistantCameraWatch_hpp_

#include "AssistantActionExecutor.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

/** The printer's own camera AI (Bambu xcam): AI print monitoring, spaghetti
 *  detection, first-layer inspection, nozzle clumping and air printing. The
 *  assistant can report which checks are on and, with the same consent as
 *  printer control, turn them on. The result is confirmed from what the
 *  printer reports, never assumed. Alerts the camera raises arrive as HMS
 *  messages, which printer_diagnostics already reads. */
class AssistantCameraWatch
{
public:
    /** {"type":"camera_watch","enable":bool?}. Without enable: status only. */
    static AssistantActionResult apply(const nlohmann::json& action);
};

}} // namespace Slic3r::GUI

#endif
