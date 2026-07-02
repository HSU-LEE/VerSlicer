#include "libslic3r/MeshAssist/MeshAssistEngine.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace Slic3r;
using namespace Slic3r::MeshAssist;

static int g_failures = 0;

static void expect_true(bool cond, const char* label)
{
    if (!cond) {
        std::cerr << "FAIL: " << label << '\n';
        ++g_failures;
    }
}

static TriangleMesh make_open_triangle()
{
    indexed_triangle_set its;
    its.vertices = {stl_vertex(0.f, 0.f, 0.f), stl_vertex(10.f, 0.f, 0.f), stl_vertex(0.f, 10.f, 0.f)};
    its.indices  = {{0, 1, 2}};
    return TriangleMesh(std::move(its));
}

int main()
{
    TriangleMesh cube = make_cube(10, 10, 10);
    const MeshHealthReport h = analyze(cube);
    expect_true(h.valid && h.manifold, "cube is manifold");
    expect_true(!needs_repair(h), "manifold cube does not need repair");
    expect_true(std::abs(h.size_mm.x() - 10.0) < 0.01, "cube size x");
    expect_true(std::abs(h.size_mm.y() - 10.0) < 0.01, "cube size y");
    expect_true(std::abs(h.size_mm.z() - 10.0) < 0.01, "cube size z");
    expect_true(h.volume_mm3 > 900.0, "cube volume_mm3");

    const MeshHealthReport open_h = analyze(make_open_triangle());
    expect_true(open_h.valid, "open triangle is valid mesh");
    expect_true(!open_h.manifold || open_h.open_edges > 0, "open triangle is non-manifold");
    expect_true(needs_repair(open_h), "open triangle needs repair");

    expect_true(mirror(cube, MirrorAxis::X), "mirror x");
    expect_true(cube.stats().number_of_facets > 0, "mirror keeps facets");

    TriangleMesh hole_target = make_cube(20, 20, 20);
    const Vec3d              center(10, 10, 10);
    expect_true(subtract_cylinder(hole_target, center, 3.0, 25.0, nullptr), "subtract cylinder");

    std::string err;
    TriangleMesh broken = make_cube(5, 5, 5);
    expect_true(repair(broken, &err), "repair cube");

    if (g_failures == 0) {
        std::cout << "mesh_assist_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "mesh_assist_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}
