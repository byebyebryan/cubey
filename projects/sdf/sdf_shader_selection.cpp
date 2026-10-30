#include "sdf_shader_selection.h"
#include "sdf_render_modes.h"
#include <stdexcept>

namespace cubey::projects::sdf {
bool cached_source_normals(const SdfConfig& config) {
    return config.cached_quality == "source-normal" ||
           config.cached_quality == "source-normal-zero" ||
           config.cached_quality == "refine-normal";
}
std::string_view cached_quality_prefix(const SdfConfig& config) {
    if (config.cached_quality == "ordinary")
        return "";
    if (config.cached_quality == "refine-0.05")
        return "cached05_";
    if (config.cached_quality == "refine-0.03125")
        return "cached03125_";
    if (config.cached_quality == "refine-normal")
        return "cachedrefinenormal_";
    if (cached_source_normals(config))
        return "cachednormal_";
    throw std::invalid_argument("unknown cached quality mode");
}
std::string_view query_step_prefix(const SdfConfig& config) {
    if (config.query_stepping == "ordinary")
        return "";
    if (config.query_stepping == "relaxed-1.2")
        return "relaxed12_";
    if (config.query_stepping == "relaxed-1.5")
        return "relaxed15_";
    if (config.query_stepping == "adaptive-0.5")
        return "adaptive50_";
    throw std::invalid_argument("unknown query stepping mode");
}
bool source_shader_variants_enabled(const SdfConfig& config) {
    return config.surface_trace != "sampled" && config.source_shaders == "automatic" &&
           config.comparison_stage == "off" && config.frame_profile == "off";
}
void validate_source_optimization(const SdfConfig& config) {
    static_cast<void>(query_step_prefix(config));
    static_cast<void>(cached_quality_prefix(config));
    if (!config.quality_output.empty() &&
        16U + static_cast<std::uint64_t>(config.common.width) * config.common.height * 16U >
            32U * 1024U * 1024U)
        throw std::invalid_argument("quality output exceeds 32 MiB logical storage cap");
    if (config.cached_quality != "ordinary" && !uses_public_cached_quality(config) &&
        (!config.common.headless || config.timeline.motion != "static" ||
         config.surface_trace != "sampled" || config.csg_evaluation != "full" ||
         config.acceleration_variant != "off" || config.region_axis != 64 ||
         config.query_stepping != "ordinary" || config.source_shaders != "automatic" ||
         (config.source_optimization != "automatic" && config.source_optimization != "reference") ||
         config.lighting != "atmosphere" || config.debug != 0 ||
         (config.supersample != 1 && config.supersample != 2) || config.frame_profile != "off" ||
         config.comparison_stage != "off" || config.ao || config.clouds || config.ground ||
         config.occluder))
        throw std::invalid_argument("cached quality study requires static headless sampled/full "
                                    "A64 atmosphere SS1/SS2, no overlays/AO/ground/clouds");
    if (!config.quality_output.empty() &&
        (!config.common.headless || config.common.frames == 0 ||
         (config.common.frames != 1 && config.comparison_actions.empty()) ||
         (config.common.frames > 1 &&
          (!config.common.profile_output_prefix.empty() || config.common.print_frame_stats)) ||
         config.timeline.motion != "static" || config.lighting != "atmosphere" ||
         config.debug != 0 || config.supersample != 1 || config.ao || config.ground ||
         config.clouds || config.occluder || !config.query_workload_output.empty() ||
         !config.comparison_input.empty() || config.comparison_stage != "off" ||
         config.frame_profile != "off" || config.source_shaders != "automatic" ||
         (config.surface_trace != "sampled" &&
          (config.surface_trace != "analytic" || config.csg_evaluation != "pruned" ||
           config.region_axis != 64 || config.acceleration_variant != "root-far" ||
           config.source_optimization != "combined"))))
        throw std::invalid_argument(
            "quality output requires static headless sampled or combined "
            "analytical A64 root-far atmosphere, SS1, no overlays/AO/ground/clouds; "
            "multiple frames require captured unchanged-geometry pose actions without profiling");
    if (config.query_stepping != "ordinary" &&
        (!config.common.headless || config.surface_trace != "analytic" ||
         config.csg_evaluation != "pruned" || config.region_axis != 64 ||
         config.acceleration_variant != "root-far" || config.timeline.motion != "static" ||
         config.lighting != "atmosphere" || config.source_shaders != "automatic" ||
         config.frame_profile != "off" || config.comparison_stage != "off" ||
         (config.source_optimization != "shadow" && config.source_optimization != "combined")))
        throw std::invalid_argument(
            "guarded stepping requires static headless analytic A64 root-far "
            "atmosphere shadow/combined study");
    if (!config.query_workload_output.empty() &&
        (!config.common.headless || config.common.frames != 1 ||
         config.surface_trace != "analytic" || config.csg_evaluation != "pruned" ||
         config.region_axis != 64 || config.acceleration_variant != "root-far" ||
         config.timeline.motion != "static" || config.lighting != "atmosphere" ||
         config.debug != 0 || config.source_shaders != "automatic" ||
         config.frame_profile != "off" || config.ao || config.clouds || config.occluder ||
         !config.comparison_actions.empty() || !config.comparison_input.empty() ||
         config.comparison_stage != "off" || config.supersample != 1 ||
         (config.source_optimization != "shadow" && config.source_optimization != "combined")))
        throw std::invalid_argument(
            "query workload requires one static headless analytic A64 "
            "root-far atmosphere frame, shadow/combined and no AO/overlays");
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
