#ifndef slic3r_OllamaAgentProactive_hpp_
#define slic3r_OllamaAgentProactive_hpp_

namespace Slic3r { namespace GUI {

/** Event-bus hooks for proactive agent triggers (slice fail, model load). */
class OllamaAgentProactive
{
public:
    static void install();
};

}} // namespace

#endif
