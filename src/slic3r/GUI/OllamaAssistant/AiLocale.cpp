#include "AiLocale.hpp"

#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"

#include <wx/intl.h>
#include <wx/translation.h>

namespace Slic3r { namespace GUI { namespace AiLocale {

bool korean()
{
    return SlicePilotUi::smart_print_locale_korean();
}

wxString text(const char* en, const char* ko_utf8)
{
    if (korean())
        return wxString::FromUTF8(ko_utf8);
    return wxGetTranslation(wxString::FromUTF8(en));
}

wxString text(const wxString& en, const char* ko_utf8)
{
    if (korean())
        return wxString::FromUTF8(ko_utf8);
    return en;
}

}}} // namespace Slic3r::GUI::AiLocale
