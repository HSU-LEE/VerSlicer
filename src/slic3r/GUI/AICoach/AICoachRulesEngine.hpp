#ifndef slic3r_AICoachRulesEngine_hpp_
#define slic3r_AICoachRulesEngine_hpp_

#include "AICoachTypes.hpp"

namespace Slic3r {
class Print;

namespace GUI {

class Plater;

class AICoachRulesEngine
{
public:
    static std::vector<AICoachCard> evaluate_after_model_load(Plater* plater);
    static std::vector<AICoachCard> evaluate_after_slice(Plater* plater, const Print* print, bool slice_ok);
    static std::vector<AICoachCard> evaluate_periodic(Plater* plater);
    /** Read-only tips while a job is printing (Monitor). */
    static std::vector<AICoachCard> evaluate_during_print(Plater* plater, int print_percent);
    /** Personalized suggestions after repeated poor outcomes (Trainer). */
    static std::vector<AICoachCard> evaluate_personal_trainer(Plater* plater);
};

}} // namespace

#endif
