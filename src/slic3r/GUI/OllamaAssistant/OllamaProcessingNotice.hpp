#ifndef slic3r_OllamaProcessingNotice_hpp_
#define slic3r_OllamaProcessingNotice_hpp_

#include <string>

namespace Slic3r { namespace GUI {

class Plater;

/** Canvas toast (orange bar + spinner) while Ollama is working. */
class OllamaProcessingNotice
{
public:
    static void show(Plater* plater, const std::string& text);
    static void hide(Plater* plater);

    /** Localized notice while the assistant is working on a request. */
    static void show_thinking(Plater* plater);
    /** Localized notice while Ollama is starting. */
    static void show_starting(Plater* plater);
};

}} // namespace

#endif
