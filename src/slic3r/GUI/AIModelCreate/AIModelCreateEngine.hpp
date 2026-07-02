#ifndef slic3r_AIModelCreateEngine_hpp_
#define slic3r_AIModelCreateEngine_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct AIModelCreateResult
{
    bool         success{ false };
    TriangleMesh mesh;
    std::string  error;
    std::string  summary;
};

TriangleMesh ai_model_create_mesh_from_sketch(const std::vector<std::vector<Vec2d>>& strokes_norm,
                                              double bed_mm = 80.0, double default_height_mm = 20.0);

bool ai_model_create_validate_mesh(const TriangleMesh& mesh, std::string* error = nullptr);

void ai_model_create_generate_async(const std::string& user_text,
                                    const std::vector<std::vector<Vec2d>>& strokes_norm,
                                    std::function<void(AIModelCreateResult)> callback);

}} // namespace

#endif
