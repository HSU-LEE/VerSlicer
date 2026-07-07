#ifndef slic3r_OllamaSendRouter_hpp_
#define slic3r_OllamaSendRouter_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/**
 * Pure routing decisions for OllamaChatPanel::on_send (no UI, no side effects).
 * The panel gathers the state inputs and performs the actual UI actions; this
 * router only answers "which path should this turn take?".
 */
class OllamaSendRouter
{
public:
    /** Pre-router acquisition gate: true when this turn is even eligible for the
     *  end-to-end find-and-print orchestrator job (mode + flag + no active job). */
    static bool acquisition_gate_open(bool apply_mode, bool orchestrator_enabled, bool orchestrator_active);

    /** True when the message is a "get me X and print it" acquisition request. */
    static bool is_acquisition_request(const std::string& user_utf8, bool plate_has_model);

    /** MakerWorld search query for an acquisition turn (falls back to the raw text). */
    static std::string acquisition_query(const std::string& user_utf8);

    /** MakerWorld bypass: message is only a MakerWorld search/import request. */
    static bool should_bypass_to_makerworld(const std::string& user_utf8);

    /** Assist-loop vs single-hop dispatch decision for a chat turn. */
    static bool should_use_assist_loop(const std::string& user_utf8, bool apply_mode, bool plate_has_model);
};

}} // namespace

#endif
