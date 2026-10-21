#ifndef slic3r_AssistantPrinterControl_hpp_
#define slic3r_AssistantPrinterControl_hpp_

#include "AssistantActionExecutor.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

class wxWindow;

namespace Slic3r { namespace GUI {

/** What the connected printer allows right now. Filled from MachineObject. */
struct PrinterControlLimits
{
    bool online{false};
    bool printing{false};
    int  nozzle_min{0};
    int  nozzle_max{300};
    int  bed_max{110};
    bool chamber_editable{false};
    int  chamber_min{0};
    int  chamber_max{60};
    int  chamber_heat_switch{40};   // at or above this the printer switches to heating mode
    bool chamber_heating_mode{false};
    bool chamber_cooling_while_printing{false};
};

enum class PrinterControlTarget
{
    NozzleTemp,
    BedTemp,
    ChamberTemp,
    PrintSpeed,
};

struct PrinterControlRequest
{
    PrinterControlTarget target;
    int                  value; // degrees C, or speed level 1-4 (silent, standard, sport, ludicrous)
};

/** The AI may change a few printer values, and only after the user allows it:
 *  nozzle, bed and chamber target temperature, and print speed level. Every
 *  value is checked against the printer's own limits; nothing else is reachable. */
class AssistantPrinterControl
{
public:
    /** Validate {"type":"printer_control","target":...,"value":...}. Pure; on
     *  failure returns nullopt and a reason the model can act on. */
    static std::optional<PrinterControlRequest> validate(const nlohmann::json& action, const PrinterControlLimits& limits,
                                                         std::string* error);

    static std::string describe(const PrinterControlRequest& req, bool korean);

    /** Has the user allowed AI printer control? Stored in app config. */
    static bool enabled();
    static void set_enabled(bool on);

    /** Ask once; returns true when allowed (now or earlier). GUI thread. */
    static bool ensure_consent(wxWindow* parent);

    /** Executor handler: consent, limits, validation, then the device command. */
    static AssistantActionResult apply(const nlohmann::json& action);
};

}} // namespace

#endif
