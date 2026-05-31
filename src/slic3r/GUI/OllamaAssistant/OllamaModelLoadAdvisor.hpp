#ifndef slic3r_OllamaModelLoadAdvisor_hpp_
#define slic3r_OllamaModelLoadAdvisor_hpp_

namespace Slic3r { namespace GUI {

class Plater;

/** On model load: ask Ollama for print settings and apply them automatically. */
class OllamaModelLoadAdvisor
{
public:
    static void schedule_after_model_load(Plater* plater);
};

}} // namespace

#endif
