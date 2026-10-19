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

bool label_mentions(const std::string& label, std::initializer_list<const char*> english_words)
{
    const wxString hay = wxString::FromUTF8(label).Lower();
    for (const char* word : english_words) {
        const wxString en = wxString::FromUTF8(word).Lower();
        if (hay.Contains(en))
            return true;
        const wxString translated = wxGetTranslation(wxString::FromUTF8(word)).Lower();
        if (!translated.empty() && translated != en && hay.Contains(translated))
            return true;
    }
    return false;
}

}}} // namespace Slic3r::GUI::AiLocale
