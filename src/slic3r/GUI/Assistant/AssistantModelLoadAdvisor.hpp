#ifndef slic3r_AssistantModelLoadAdvisor_hpp_
#define slic3r_AssistantModelLoadAdvisor_hpp_

namespace Slic3r { namespace GUI {

class Plater;

/** On model load: ask Assistant for print settings and apply them automatically. */
class AssistantModelLoadAdvisor
{
public:
    static void schedule_after_model_load(Plater* plater);
};

}} // namespace

#endif
