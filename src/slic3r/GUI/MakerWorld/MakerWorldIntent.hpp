#ifndef slic3r_MakerWorldIntent_hpp_
#define slic3r_MakerWorldIntent_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/**
 * Keyword/intent detection for MakerWorld requests (moved out of
 * MakerWorldSearchService; no network / no UI).
 */
class MakerWorldIntent
{
public:
    static bool user_wants_makerworld_search(const std::string& user_text);
    static bool user_wants_makerworld_import(const std::string& user_text);

    /** True when the message is only MakerWorld search/import (no other slicer actions). */
    static bool is_pure_makerworld_request(const std::string& user_text);

    static bool is_informational_makerworld_question(const std::string& user_text);
};

}} // namespace

#endif
