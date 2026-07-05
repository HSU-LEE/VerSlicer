#ifndef slic3r_AiLocale_hpp_
#define slic3r_AiLocale_hpp_

#include <wx/string.h>

namespace Slic3r { namespace GUI { namespace AiLocale {

/**
 * Single locale predicate shared by all AI surfaces (Ollama chat, AI pipeline,
 * MakerWorld dialogs, Smart Print planner glue). Delegates to the design-system
 * check so there is exactly one definition of "the UI is Korean".
 *
 * Rationale: the ko_KR gettext catalog does not yet carry the AI-assistant
 * strings, so these surfaces keep a bilingual dual path. English msgids still
 * go through wxGetTranslation so a future catalog pass picks them up without
 * code changes.
 */
bool korean();

/** Bilingual pick: Korean UTF-8 literal when the UI is Korean, else the
 *  (gettext-translated) English string. */
wxString text(const char* en, const char* ko_utf8);

/** Same, for an already-translated English wxString (e.g. _L("...")). */
wxString text(const wxString& en, const char* ko_utf8);

}}} // namespace Slic3r::GUI::AiLocale

#endif
