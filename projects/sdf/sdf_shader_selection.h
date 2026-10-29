#pragma once
#include "sdf_config.h"
#include <cstddef>
#include <string_view>

namespace cubey::projects::sdf {
enum class SourceShaderRoute : unsigned {
    General,
    LitGeneral,
    LitSingleMaterial
};
[[nodiscard]] bool source_shader_variants_enabled(const SdfConfig& config);
// Count comes from the published source/header, never the requested scene.
[[nodiscard]] SourceShaderRoute select_source_shader(const SdfConfig& config,
                                                     std::size_t published_material_count);
[[nodiscard]] std::string_view source_shader_route_name(SourceShaderRoute route);
} // namespace cubey::projects::sdf
