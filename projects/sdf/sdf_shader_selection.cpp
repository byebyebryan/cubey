#include "sdf_shader_selection.h"
#include <stdexcept>

namespace cubey::projects::sdf {
bool source_shader_variants_enabled(const SdfConfig& config) {
    return config.surface_trace != "sampled" && config.source_shaders == "automatic" &&
           config.comparison_stage == "off" && config.frame_profile == "off";
}
SourceShaderRoute select_source_shader(const SdfConfig& config, std::size_t material_count) {
    if (!source_shader_variants_enabled(config) || config.debug != 0)
        return SourceShaderRoute::General;
    return material_count == 1 && config.material_fast_path ? SourceShaderRoute::LitSingleMaterial
                                                            : SourceShaderRoute::LitGeneral;
}
std::string_view source_shader_route_name(SourceShaderRoute route) {
    switch (route) {
    case SourceShaderRoute::General:
        return "general";
    case SourceShaderRoute::LitGeneral:
        return "lit-general";
    case SourceShaderRoute::LitSingleMaterial:
        return "lit-single-material";
    }
    throw std::logic_error("invalid source shader route");
}
} // namespace cubey::projects::sdf
