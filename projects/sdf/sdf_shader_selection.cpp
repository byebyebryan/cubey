#include "sdf_shader_selection.h"
#include <stdexcept>

namespace cubey::projects::sdf {
bool source_shader_variants_enabled(const SdfConfig& config) {
    return config.surface_trace != "sampled" && config.source_shaders == "automatic" &&
           config.comparison_stage == "off" && config.frame_profile == "off";
}
void validate_source_optimization(const SdfConfig& config) {
    const auto& mode = config.source_optimization;
    if (mode == "automatic" || mode == "reference")
        return;
    if (!source_shader_variants_enabled(config))
        throw std::invalid_argument(
            "explicit source optimization requires automatic source shaders without diagnostics");
    if ((mode == "prepared-cones" || mode == "combined") &&
        (!config.common.headless || config.surface_trace != "analytic" ||
         config.csg_evaluation != "pruned" || config.region_axis != 64 ||
         config.acceleration_variant != "root-far" || config.timeline.motion != "static" ||
         config.lighting != "atmosphere"))
        throw std::invalid_argument("prepared-cones study requires static headless analytic pruned "
                                    "root-far atmosphere at axis 64");
    if (mode != "shadow" && mode != "prepared-cones" && mode != "combined")
        throw std::invalid_argument("unknown source optimization");
}
bool source_shadow_pipeline_enabled(const SdfConfig& config) {
    return source_shader_variants_enabled(config) && config.lighting == "atmosphere" &&
           (config.source_optimization == "shadow" || config.source_optimization == "automatic" ||
            config.source_optimization == "combined");
}
bool source_prepared_cones_enabled(const SdfConfig& config) {
    return source_shader_variants_enabled(config) &&
           (config.source_optimization == "prepared-cones" ||
            config.source_optimization == "combined");
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
