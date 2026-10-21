#include "libslic3r/BambuSmartPrint/PrintGoalParser.hpp"
#include "libslic3r/BambuSmartPrint/ChangeEffects.hpp"
#include "libslic3r/BambuSmartPrint/PrintPlanner.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cstdlib>
#include <iostream>

using namespace Slic3r;
using namespace Slic3r::BambuSmartPrint;

static int g_failures = 0;

static void expect_true(bool cond, const char* label)
{
    if (!cond) {
        std::cerr << "FAIL: " << label << '\n';
        ++g_failures;
    }
}

static PlateContext minimal_plate_context()
{
    PlateContext ctx;
    ctx.has_model = true;
    ctx.base_config.set_key_value("wall_loops", new ConfigOptionInt(2));
    ctx.base_config.set_key_value("layer_height", new ConfigOptionFloat(0.2));
    ctx.base_config.set_key_value("sparse_infill_density", new ConfigOptionPercent(15));
    ctx.base_config.set_key_value("brim_width", new ConfigOptionFloat(0));
    ctx.base_config.set_key_value("enable_support", new ConfigOptionBool(false));
    ctx.proposed_config = ctx.base_config;
    return ctx;
}

int main()
{
    {
        using Slic3r::BambuSmartPrint::ChangeEffects;
        using Slic3r::BambuSmartPrint::SettingChange;
        const auto stronger = ChangeEffects::estimate({{"wall_loops", "2", "4", ""}, {"sparse_infill_density", "15%", "35%", ""}});
        expect_true(stronger.strength > 0 && stronger.time > 0 && stronger.filament > 0, "more walls and infill: stronger, longer, more");
        const auto finer = ChangeEffects::estimate({{"layer_height", "0.2", "0.12", ""}});
        expect_true(finer.surface > 0 && finer.time > 0, "thinner layers: smoother, longer");
        const auto brim = ChangeEffects::estimate({{"brim_type", "no_brim", "outer_only", ""}});
        expect_true(brim.adhesion > 0, "brim improves adhesion");
        expect_true(ChangeEffects::estimate({}).empty(), "no changes, no effect");
        expect_true(stronger.summary().find("Strength") != std::string::npos, "summary names strength");
    }
    {
        PrintGoal g = PrintGoalParser::parse("an outdoor part, make it strong");
        expect_true(g.user_text.find("strong") != std::string::npos, "parser keeps the user text");
        expect_true(g.intents.empty(), "parser does not assign intents from keywords");
    }

    {
        PlateContext ctx;
        ctx.has_model = true;
        ctx.mesh.overhang_face_ratio = 0.25;
        const auto risks = PrintPlanner::build_risks(ctx);
        bool has_overhang = false;
        for (const PrintRisk& r : risks) {
            if (r.kind == PrintRiskKind::Overhang) {
                has_overhang = true;
                expect_true(r.recommended == RecommendedAction::EnableSupport, "overhang action");
            }
        }
        expect_true(has_overhang, "overhang risk detected");
    }

    auto goal_with = [](PrintGoalIntent intent, float strong = 0.f, float fast = 0.f) {
        PrintGoal g;
        g.intents.push_back(intent);
        g.weight_strong = strong;
        g.weight_fast = fast;
        return g;
    };

    {
        PrintGoal a = goal_with(PrintGoalIntent::Strong, 0.8f, 0.f);
        PrintGoal b = goal_with(PrintGoalIntent::Fast, 0.f, 0.8f);
        PrintGoal m = PrintGoalParser::merge(a, b);
        expect_true(m.has_intent(PrintGoalIntent::Strong) && m.has_intent(PrintGoalIntent::Fast),
                    "merged dual intent");
    }

    {
        PrintGoal g = goal_with(PrintGoalIntent::Strong, 0.8f, 0.f);
        g.intents.push_back(PrintGoalIntent::Fast);
        g.weight_fast = 0.8f;
        expect_true(!PrintPlanner::build_tradeoff_note(g).empty(), "tradeoff note for conflict");
    }

    {
        PlateContext ctx = minimal_plate_context();
        PrintGoal      g = goal_with(PrintGoalIntent::Strong, 0.85f, 0.f);
        PrintPlan      plan = PrintPlanner::plan_without_llm(ctx, g);
        expect_true(plan.proposed_config.opt_int("wall_loops") >= 3, "strong goal raises wall_loops");
        expect_true(plan.has_actions(), "strong goal produces actions");
    }

    {
        PlateContext ctx = minimal_plate_context();
        PrintGoal      g = goal_with(PrintGoalIntent::Fast, 0.f, 0.85f);
        PrintPlan      plan = PrintPlanner::plan_without_llm(ctx, g);
        expect_true(plan.proposed_config.opt_float("layer_height") >= 0.24, "fast goal raises layer height");
    }

    {
        PlateContext ctx = minimal_plate_context();
        ctx.mesh.needs_brim = true;
        PrintGoal g = goal_with(PrintGoalIntent::Adhesion);
        PrintPlan plan = PrintPlanner::plan_without_llm(ctx, g);
        expect_true(plan.proposed_config.opt_float("brim_width") >= 5.0, "adhesion goal adds brim");
    }

    {
        PlateContext ctx = minimal_plate_context();
        ctx.mesh.overhang_face_ratio = 0.2;
        PrintGoal g = goal_with(PrintGoalIntent::Overhang);
        PrintPlan plan = PrintPlanner::plan_without_llm(ctx, g);
        expect_true(plan.proposed_config.opt_bool("enable_support"), "overhang goal enables support");
    }

    if (g_failures == 0) {
        std::cout << "print_planner_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "print_planner_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
