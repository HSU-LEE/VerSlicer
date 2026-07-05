#ifndef slic3r_AIPipeline_PrintJobUiAdapter_hpp_
#define slic3r_AIPipeline_PrintJobUiAdapter_hpp_

#include "PrintJob.hpp"

#include "../MakerWorld/MakerWorldImportFlow.hpp" // MakerWorldFlowUiCallbacks

#include <utility>

namespace Slic3r { namespace GUI { namespace AIPipeline {

/**
 * Header-only bridge between the legacy Phase 1 MakerWorldFlowUiCallbacks and the
 * Phase 3 PrintJobUiCallbacks. Lets the orchestrator reuse OllamaChatPanel's
 * existing chat/busy/status plumbing (makerworld_flow_callbacks()) unchanged.
 *
 * Both callback sets are invoked only on the wx main thread by their producers,
 * so no additional marshaling is introduced here.
 */
class PrintJobUiAdapter
{
public:
    /** Adapt legacy MakerWorld callbacks into orchestrator callbacks. */
    static PrintJobUiCallbacks from_makerworld(MakerWorldFlowUiCallbacks mw)
    {
        PrintJobUiCallbacks ui;
        if (mw.append_chat) {
            ui.append_chat = [fn = std::move(mw.append_chat)](const wxString& msg) { fn(msg); };
        }
        if (mw.set_busy) {
            ui.set_busy = [fn = std::move(mw.set_busy)](bool busy, const wxString& status) { fn(busy, status); };
        }
        if (mw.on_flow_finished) {
            ui.on_finished = [fn = std::move(mw.on_flow_finished)]() { fn(); };
        }
        return ui;
    }

    /** Adapt orchestrator callbacks back into legacy MakerWorld callbacks. */
    static MakerWorldFlowUiCallbacks to_makerworld(PrintJobUiCallbacks ui)
    {
        MakerWorldFlowUiCallbacks mw;
        if (ui.append_chat) {
            mw.append_chat = [fn = std::move(ui.append_chat)](const wxString& msg) { fn(msg); };
        }
        if (ui.set_busy) {
            mw.set_busy = [fn = std::move(ui.set_busy)](bool busy, const wxString& status) { fn(busy, status); };
        }
        if (ui.on_finished) {
            mw.on_flow_finished = [fn = std::move(ui.on_finished)]() { fn(); };
        }
        return mw;
    }
};

}}} // namespace Slic3r::GUI::AIPipeline

#endif
