#ifndef slic3r_AiCatalog_hpp_
#define slic3r_AiCatalog_hpp_

#include <string>

#ifndef OLLAMA_HEADLESS_TEST
#include "AiLocale.hpp"
#endif

namespace Slic3r { namespace GUI {

inline std::string ai_catalog_text(const char* english)
{
#ifdef OLLAMA_HEADLESS_TEST
    return english;
#else
    return AiLocale::text(english).utf8_string();
#endif
}

}} // namespace Slic3r::GUI

#endif
