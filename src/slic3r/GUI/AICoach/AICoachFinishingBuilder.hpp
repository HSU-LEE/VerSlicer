#ifndef slic3r_AICoachFinishingBuilder_hpp_
#define slic3r_AICoachFinishingBuilder_hpp_

#include "AICoachTypes.hpp"

namespace Slic3r { namespace GUI {

class Plater;

struct AICoachFinishingBuilder
{
    static AICoachCard build_print_success_card(Plater* plater, const std::string& job_name);
};

}} // namespace

#endif
