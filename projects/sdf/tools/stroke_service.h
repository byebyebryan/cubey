#pragma once
#include "../sdf_cached_stroke.h"
#include "../sdf_config.h"
#include <nlohmann/json.hpp>

namespace cubey::projects::sdf {
int run_cached_stroke_service(SdfConfig config, const nlohmann::json& input,
                              const CachedStrokeProgram& program,
                              const std::filesystem::path& output);
}
