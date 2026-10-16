#include <cubey/render/cloud_layer.h>

#include <cubey/render/generated_texture.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::render {

namespace {

[[nodiscard]] float sampling_mode_value(CloudLayerSamplingMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float view_sample_mode_value(CloudLayerViewSampleMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float background_mode_value(CloudLayerBackgroundMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float scene_depth_mode_value(CloudLayerSceneDepthMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float distance_mode_value(CloudLayerDistanceMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float orbit_representation_value(CloudLayerOrbitRepresentation mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float density_model_value(CloudLayerDensityModel model) {
    return static_cast<float>(static_cast<std::uint32_t>(model));
}

[[nodiscard]] float resolve_mode_value(CloudLayerResolveMode mode) {
    return static_cast<float>(static_cast<std::uint32_t>(mode));
}

[[nodiscard]] float cloud_style_value(CloudLayerCloudStyle style) {
    return static_cast<float>(static_cast<std::uint32_t>(style));
}


} // namespace

CloudLayerShadowProjection cloud_layer_shadow_projection(const CloudLayerShadowRequest& request) {
    if (!std::isfinite(request.half_extent_m) || request.half_extent_m <= 0.0F) {
        throw std::runtime_error("cloud shadow half extent must be finite and positive");
    }
    const float axis_u_length = glm::length(request.receiver_axis_u);
    if (!std::isfinite(axis_u_length) || axis_u_length <= 0.0001F) {
        throw std::runtime_error("cloud shadow receiver U axis must be finite and non-zero");
    }
    const math::Vec3 axis_u = request.receiver_axis_u / axis_u_length;
    const math::Vec3 axis_v_unprojected =
        request.receiver_axis_v - axis_u * glm::dot(request.receiver_axis_v, axis_u);
    const float axis_v_length = glm::length(axis_v_unprojected);
    if (!std::isfinite(axis_v_length) || axis_v_length <= 0.0001F) {
        throw std::runtime_error("cloud shadow receiver axes must not be collinear");
    }
    const math::Vec3 axis_v = axis_v_unprojected / axis_v_length;
    const VkExtent2D extent{kCloudLayerShadowTextureSize, kCloudLayerShadowTextureSize};
    const float texel_world_size =
        (2.0F * request.half_extent_m) / static_cast<float>(extent.width);
    const float center_u = glm::dot(request.receiver_center, axis_u);
    const float center_v = glm::dot(request.receiver_center, axis_v);
    const float snapped_u = std::round(center_u / texel_world_size) * texel_world_size;
    const float snapped_v = std::round(center_v / texel_world_size) * texel_world_size;
    const math::Vec3 snapped_center = request.receiver_center + axis_u * (snapped_u - center_u) +
                                      axis_v * (snapped_v - center_v);
    const float world_to_uv_scale = 1.0F / (2.0F * request.half_extent_m);

    return {
        .receiver_center = snapped_center,
        .receiver_axis_u = axis_u,
        .receiver_axis_v = axis_v,
        .world_to_uv_x =
            {
                axis_u.x * world_to_uv_scale,
                axis_u.y * world_to_uv_scale,
                axis_u.z * world_to_uv_scale,
                0.5F - glm::dot(snapped_center, axis_u) * world_to_uv_scale,
            },
        .world_to_uv_y =
            {
                axis_v.x * world_to_uv_scale,
                axis_v.y * world_to_uv_scale,
                axis_v.z * world_to_uv_scale,
                0.5F - glm::dot(snapped_center, axis_v) * world_to_uv_scale,
            },
        .extent = extent,
        .texel_world_size_m = texel_world_size,
    };
}

CloudLayerQualityBudget cloud_layer_quality_budget(CloudLayerQuality quality) {
    switch (quality) {
    case CloudLayerQuality::Quarter:
        return {.view_steps = 32, .light_steps = 3, .resolution_scale = 0.25F};
    case CloudLayerQuality::Half:
        return {.view_steps = 48, .light_steps = 4, .resolution_scale = 0.5F};
    case CloudLayerQuality::Full:
        return {.view_steps = 64, .light_steps = 6, .resolution_scale = 1.0F};
    }
    return {};
}

CloudLayerDebugView cloud_layer_debug_view_from_name(std::string_view value) {
    if (value.empty() || value == "final") {
        return CloudLayerDebugView::Final;
    }
    if (value == "raw-final") {
        return CloudLayerDebugView::RawFinal;
    }
    if (value == "authored-weather" || value == "weather") {
        return CloudLayerDebugView::AuthoredWeather;
    }
    if (value == "density") {
        return CloudLayerDebugView::Density;
    }
    if (value == "transmittance") {
        return CloudLayerDebugView::Transmittance;
    }
    if (value == "lighting") {
        return CloudLayerDebugView::Lighting;
    }
    if (value == "ambient-light") {
        return CloudLayerDebugView::AmbientLight;
    }
    if (value == "direct-light") {
        return CloudLayerDebugView::DirectLight;
    }
    if (value == "phase-light") {
        return CloudLayerDebugView::PhaseLight;
    }
    if (value == "shadow") {
        return CloudLayerDebugView::Shadow;
    }
    if (value == "steps") {
        return CloudLayerDebugView::Steps;
    }
    if (value == "background") {
        return CloudLayerDebugView::Background;
    }
    if (value == "cloud-alpha") {
        return CloudLayerDebugView::CloudAlpha;
    }
    if (value == "distance") {
        return CloudLayerDebugView::Distance;
    }
    if (value == "metadata-distance") {
        return CloudLayerDebugView::MetadataDistance;
    }
    if (value == "metadata-alpha") {
        return CloudLayerDebugView::MetadataAlpha;
    }
    if (value == "metadata-confidence") {
        return CloudLayerDebugView::MetadataConfidence;
    }
    if (value == "metadata-density") {
        return CloudLayerDebugView::MetadataDensity;
    }
    if (value == "base-density") {
        return CloudLayerDebugView::BaseDensity;
    }
    if (value == "detail-density") {
        return CloudLayerDebugView::DetailDensity;
    }
    if (value == "cloud-type") {
        return CloudLayerDebugView::CloudType;
    }
    if (value == "local-scatter") {
        return CloudLayerDebugView::LocalScatter;
    }
    if (value == "local-clear") {
        return CloudLayerDebugView::LocalClear;
    }
    if (value == "local-structure") {
        return CloudLayerDebugView::LocalStructure;
    }
    if (value == "local-edge-detail") {
        return CloudLayerDebugView::LocalEdgeDetail;
    }
    if (value == "weather-edge") {
        return CloudLayerDebugView::WeatherEdge;
    }
    if (value == "coverage-bias" || value == "weather-bias" || value == "weather-mask") {
        return CloudLayerDebugView::CoverageBias;
    }
    if (value == "visible-density") {
        return CloudLayerDebugView::VisibleDensity;
    }
    if (value == "visible-cloud-type") {
        return CloudLayerDebugView::VisibleCloudType;
    }
    if (value == "distance-regime") {
        return CloudLayerDebugView::DistanceRegime;
    }
    if (value == "transition-weights") {
        return CloudLayerDebugView::TransitionWeights;
    }
    if (value == "scene-depth-occlusion" || value == "depth-occlusion" ||
        value == "depth-mask") {
        return CloudLayerDebugView::SceneDepthOcclusion;
    }
    if (value == "local-alpha") {
        return CloudLayerDebugView::LocalAlpha;
    }
    if (value == "far-shell-alpha") {
        return CloudLayerDebugView::FarShellAlpha;
    }
    if (value == "local-with-shell-alpha") {
        return CloudLayerDebugView::LocalWithShellAlpha;
    }
    if (value == "orbit-alpha") {
        return CloudLayerDebugView::OrbitAlpha;
    }
    if (value == "orbit-weather" || value == "orbit-coverage") {
        return CloudLayerDebugView::OrbitCoverage;
    }
    if (value == "orbit-detail") {
        return CloudLayerDebugView::OrbitDetail;
    }
    if (value == "orbit-hull") {
        return CloudLayerDebugView::OrbitHull;
    }
    if (value == "orbit-envelope") {
        return CloudLayerDebugView::OrbitEnvelope;
    }
    if (value == "orbit-shell-alpha" || value == "shell-alpha") {
        return CloudLayerDebugView::OrbitShellAlpha;
    }
    if (value == "orbit-shell-height" || value == "shell-height") {
        return CloudLayerDebugView::OrbitShellHeight;
    }
    if (value == "orbit-shell-normal" || value == "shell-normal") {
        return CloudLayerDebugView::OrbitShellNormal;
    }
    if (value == "orbit-shell-shadow" || value == "shell-shadow") {
        return CloudLayerDebugView::OrbitShellShadow;
    }
    if (value == "orbit-shell-footprint" || value == "shell-footprint") {
        return CloudLayerDebugView::OrbitShellFootprint;
    }
    if (value == "orbit-shell-filter" || value == "shell-filter") {
        return CloudLayerDebugView::OrbitShellFilter;
    }
    if (value == "orbit-shell-mass" || value == "shell-mass") {
        return CloudLayerDebugView::OrbitShellMass;
    }
    if (value == "jitter-pattern" || value == "jitter") {
        return CloudLayerDebugView::JitterPattern;
    }
    if (value == "horizon-step-length" || value == "horizon-steps") {
        return CloudLayerDebugView::HorizonStepLength;
    }
    if (value == "horizon-filter-lod" || value == "horizon-lod") {
        return CloudLayerDebugView::HorizonFilterLod;
    }
    if (value == "horizon-handoff" || value == "horizon-gate") {
        return CloudLayerDebugView::HorizonHandoff;
    }
    if (value == "local-truncation" || value == "horizon-local-truncation") {
        return CloudLayerDebugView::LocalTruncation;
    }
    if (value == "integrated-horizon-alpha" || value == "horizon-integrated-alpha") {
        return CloudLayerDebugView::IntegratedHorizonAlpha;
    }
    if (value == "integrated-horizon-radiance" || value == "horizon-integrated-radiance") {
        return CloudLayerDebugView::IntegratedHorizonRadiance;
    }
    if (value == "edge-mask" || value == "resolve-edge") {
        return CloudLayerDebugView::EdgeMask;
    }
    throw std::runtime_error("unknown cloud debug view: " + std::string(value));
}

const char* cloud_layer_debug_view_name(CloudLayerDebugView view) {
    switch (view) {
    case CloudLayerDebugView::Final:
        return "final";
    case CloudLayerDebugView::RawFinal:
        return "raw-final";
    case CloudLayerDebugView::AuthoredWeather:
        return "authored-weather";
    case CloudLayerDebugView::Density:
        return "density";
    case CloudLayerDebugView::Transmittance:
        return "transmittance";
    case CloudLayerDebugView::Lighting:
        return "lighting";
    case CloudLayerDebugView::AmbientLight:
        return "ambient-light";
    case CloudLayerDebugView::DirectLight:
        return "direct-light";
    case CloudLayerDebugView::PhaseLight:
        return "phase-light";
    case CloudLayerDebugView::Shadow:
        return "shadow";
    case CloudLayerDebugView::Steps:
        return "steps";
    case CloudLayerDebugView::Background:
        return "background";
    case CloudLayerDebugView::CloudAlpha:
        return "cloud-alpha";
    case CloudLayerDebugView::Distance:
        return "distance";
    case CloudLayerDebugView::MetadataDistance:
        return "metadata-distance";
    case CloudLayerDebugView::MetadataAlpha:
        return "metadata-alpha";
    case CloudLayerDebugView::MetadataConfidence:
        return "metadata-confidence";
    case CloudLayerDebugView::MetadataDensity:
        return "metadata-density";
    case CloudLayerDebugView::BaseDensity:
        return "base-density";
    case CloudLayerDebugView::DetailDensity:
        return "detail-density";
    case CloudLayerDebugView::CloudType:
        return "cloud-type";
    case CloudLayerDebugView::LocalScatter:
        return "local-scatter";
    case CloudLayerDebugView::LocalClear:
        return "local-clear";
    case CloudLayerDebugView::LocalStructure:
        return "local-structure";
    case CloudLayerDebugView::LocalEdgeDetail:
        return "local-edge-detail";
    case CloudLayerDebugView::WeatherEdge:
        return "weather-edge";
    case CloudLayerDebugView::CoverageBias:
        return "coverage-bias";
    case CloudLayerDebugView::VisibleDensity:
        return "visible-density";
    case CloudLayerDebugView::VisibleCloudType:
        return "visible-cloud-type";
    case CloudLayerDebugView::DistanceRegime:
        return "distance-regime";
    case CloudLayerDebugView::TransitionWeights:
        return "transition-weights";
    case CloudLayerDebugView::SceneDepthOcclusion:
        return "scene-depth-occlusion";
    case CloudLayerDebugView::LocalAlpha:
        return "local-alpha";
    case CloudLayerDebugView::FarShellAlpha:
        return "far-shell-alpha";
    case CloudLayerDebugView::LocalWithShellAlpha:
        return "local-with-shell-alpha";
    case CloudLayerDebugView::OrbitAlpha:
        return "orbit-alpha";
    case CloudLayerDebugView::OrbitCoverage:
        return "orbit-coverage";
    case CloudLayerDebugView::OrbitDetail:
        return "orbit-detail";
    case CloudLayerDebugView::OrbitHull:
        return "orbit-hull";
    case CloudLayerDebugView::OrbitEnvelope:
        return "orbit-envelope";
    case CloudLayerDebugView::OrbitShellAlpha:
        return "orbit-shell-alpha";
    case CloudLayerDebugView::OrbitShellHeight:
        return "orbit-shell-height";
    case CloudLayerDebugView::OrbitShellNormal:
        return "orbit-shell-normal";
    case CloudLayerDebugView::OrbitShellShadow:
        return "orbit-shell-shadow";
    case CloudLayerDebugView::OrbitShellFootprint:
        return "orbit-shell-footprint";
    case CloudLayerDebugView::OrbitShellFilter:
        return "orbit-shell-filter";
    case CloudLayerDebugView::OrbitShellMass:
        return "orbit-shell-mass";
    case CloudLayerDebugView::JitterPattern:
        return "jitter-pattern";
    case CloudLayerDebugView::HorizonStepLength:
        return "horizon-step-length";
    case CloudLayerDebugView::HorizonFilterLod:
        return "horizon-filter-lod";
    case CloudLayerDebugView::HorizonHandoff:
        return "horizon-handoff";
    case CloudLayerDebugView::LocalTruncation:
        return "local-truncation";
    case CloudLayerDebugView::IntegratedHorizonAlpha:
        return "integrated-horizon-alpha";
    case CloudLayerDebugView::IntegratedHorizonRadiance:
        return "integrated-horizon-radiance";
    case CloudLayerDebugView::EdgeMask:
        return "edge-mask";
    }
    return "final";
}

CloudLayerDebugView next_cloud_layer_debug_view(CloudLayerDebugView view) {
    const auto it = std::find(kCloudLayerDebugViews.begin(), kCloudLayerDebugViews.end(), view);
    if (it == kCloudLayerDebugViews.end() || std::next(it) == kCloudLayerDebugViews.end()) {
        return kCloudLayerDebugViews.front();
    }
    return *std::next(it);
}

VkExtent2D cloud_layer_product_extent(VkExtent2D target_extent, CloudLayerQuality quality) {
    const CloudLayerQualityBudget budget = cloud_layer_quality_budget(quality);
    return {
        .width = std::max(1U, static_cast<std::uint32_t>(
                                  std::round(static_cast<float>(target_extent.width) *
                                             budget.resolution_scale))),
        .height = std::max(1U, static_cast<std::uint32_t>(
                                   std::round(static_cast<float>(target_extent.height) *
                                              budget.resolution_scale))),
    };
}

CloudLayerFrameUniforms cloud_layer_frame_uniforms(const CloudLayerConfig& config,
                                                   const CloudLayerFrameInfo& frame) {
    const float aspect = frame.target_extent.height == 0U
                             ? 1.0F
                             : static_cast<float>(frame.target_extent.width) /
                                   static_cast<float>(frame.target_extent.height);
    const CloudLayerQualityBudget budget = cloud_layer_quality_budget(config.quality);
    const std::int32_t view_steps =
        config.view_steps_override > 0 ? std::clamp(config.view_steps_override, 1, 128)
                                       : budget.view_steps;
    const std::int32_t view_samples = config.view_samples >= 4 ? 4
                                     : config.view_samples >= 2 ? 2
                                                               : 1;
    const math::Vec3 cloud_top_color{1.12F, 1.04F, 0.82F};
    const math::Vec3 cloud_bottom_color{0.24F, 0.30F, 0.38F};

    return {
        .camera_right_aspect = {frame.camera_right.x, frame.camera_right.y, frame.camera_right.z,
                                aspect},
        .camera_up_tan_half_fovy = {frame.camera_up.x, frame.camera_up.y, frame.camera_up.z,
                                    frame.tan_half_fovy},
        .camera_forward_mode = {frame.camera_forward.x, frame.camera_forward.y,
                                frame.camera_forward.z, frame.camera_mode},
        .camera_position_radius = {frame.camera_position.x, frame.camera_position.y,
                                   frame.camera_position.z, config.planet_radius_m},
        .cloud_shell = {config.bottom_altitude_m,
                        config.top_altitude_m - config.bottom_altitude_m,
                        config.vertical_shear_fraction, cloud_style_value(config.cloud_style)},
        .weather = {config.coverage, config.density, config.weather_scale_km,
                    config.wind_offset_m},
        .sun_direction_intensity = {frame.sun_direction.x, frame.sun_direction.y,
                                    frame.sun_direction.z, frame.sun_intensity},
        .sun_color = {frame.sun_color.x, frame.sun_color.y, frame.sun_color.z, 0.0F},
        .moon_direction_intensity = {frame.moon_direction.x, frame.moon_direction.y,
                                     frame.moon_direction.z, frame.moon_intensity},
        .moon_color = {frame.moon_color.x, frame.moon_color.y, frame.moon_color.z, 0.0F},
        .ambient_color_intensity = {frame.ambient_color.x, frame.ambient_color.y,
                                    frame.ambient_color.z, frame.ambient_intensity},
        .ref_options = {static_cast<float>(static_cast<std::uint32_t>(config.debug_view)),
                        static_cast<float>(view_steps),
                        static_cast<float>(budget.light_steps),
                        static_cast<float>(frame.target_extent.width)},
        .shape_options = {config.crispiness, config.curliness, config.absorption,
                          config.powder_enabled ? config.powder_strength : 0.0F},
        .weather_feature_weights = {config.weather_fronts, config.weather_cells,
                                    config.weather_streaks, config.detail_erosion},
        .cloud_color_top_shadow = {cloud_top_color.x, cloud_top_color.y, cloud_top_color.z,
                                   config.shadow_strength},
        .cloud_color_bottom_horizon = {cloud_bottom_color.x, cloud_bottom_color.y,
                                       cloud_bottom_color.z, config.horizon_strength},
        .lighting_strengths = {config.ambient_strength, config.direct_strength,
                               config.phase_strength, config.sun_glare_strength},
        .twilight_options = {config.twilight_color_strength, config.twilight_edge_strength,
                             config.twilight_saturation_strength, config.afterglow_strength},
        .composite_options = {config.resolve_strength, config.final_contrast,
                              config.final_saturation, config.horizon_glow_strength},
        .sampling_options = {sampling_mode_value(config.sampling_mode), config.jitter_strength,
                             config.weather_softness, config.weather_influence},
        .temporal_options = {static_cast<float>(frame.temporal_frame_index % 256U),
                             config.temporal_enabled ? 1.0F : 0.0F, 0.14F,
                             view_sample_mode_value(config.view_sample_mode)},
        .background_options = {background_mode_value(config.background_mode),
                               config.horizon_layer_enabled ? 1.0F : 0.0F,
                               config.local_volume_enabled ? 1.0F : 0.0F,
                               frame.external_background ? 1.0F : 0.0F},
        .distance_options = {distance_mode_value(config.distance_mode),
                             config.orbit_transition_start_m, config.orbit_transition_end_m,
                             config.orbit_detail_strength},
        .orbit_options = {config.far_shell_start_m, config.far_shell_end_m,
                          config.orbit_density_scale,
                          orbit_representation_value(config.orbit_representation)},
        .orbit_shell_options = {config.orbit_motion_strength, config.orbit_shell_extinction,
                                config.orbit_fill, config.far_shell_strength},
        .scene_depth_options = {std::max(frame.near_plane_m, 0.0001F),
                                std::max(frame.far_plane_m,
                                         std::max(frame.near_plane_m, 0.0001F) + 1.0F),
                                scene_depth_mode_value(frame.scene_depth_mode),
                                std::max(frame.scene_depth_fade_m, 1.0F)},
        .density_options = {density_model_value(config.density_model), config.shape_domain_km,
                            config.footprint_filter_strength, static_cast<float>(view_samples)},
        .edge_options = {config.edge_softness, config.edge_detail_fade,
                         config.edge_resolve_strength, resolve_mode_value(config.resolve_mode)},
    };
}

CloudLayerViewRegime cloud_layer_view_regime(const CloudLayerViewRegimeInput& input) {
    const float radius = std::max(input.planet_radius_m, 1.0F);
    const float camera_radius = glm::length(input.camera_position);
    const float altitude = std::max(camera_radius - radius, 0.0F);
    const float transition_start = std::max(input.orbit_transition_start_m, 1.0F);
    const float transition_end = std::max(input.orbit_transition_end_m, transition_start + 1.0F);
    const float altitude_blend =
        std::clamp((altitude - transition_start) / (transition_end - transition_start), 0.0F,
                   1.0F);

    math::Vec3 up{0.0F, 1.0F, 0.0F};
    if (camera_radius > 0.0001F) {
        up = input.camera_position / camera_radius;
    }
    math::Vec3 forward{0.0F, 0.0F, -1.0F};
    if (glm::dot(input.camera_forward, input.camera_forward) > 0.000001F) {
        forward = glm::normalize(input.camera_forward);
    }
    const float view_up = std::abs(glm::dot(forward, up));
    const float horizon_grazing = 1.0F - std::clamp(view_up / 0.28F, 0.0F, 1.0F);

    float camera_mode = 0.0F;
    if (altitude_blend >= 0.95F) {
        camera_mode = 4.0F;
    } else if (altitude_blend >= 0.05F || (altitude >= 5000.0F && horizon_grazing > 0.75F)) {
        camera_mode = 1.0F;
    }

    return {
        .camera_mode = camera_mode,
        .altitude_m = altitude,
        .altitude_blend = altitude_blend,
        .horizon_grazing = horizon_grazing,
    };
}

CloudLayerWeatherPushConstants cloud_layer_weather_push_constants(
    const CloudLayerConfig& config) {
    return {
        .fronts = config.weather_fronts,
        .cells = config.weather_cells,
        .streaks = config.weather_streaks,
        .cloud_style = cloud_style_value(config.cloud_style),
        .density_model = density_model_value(config.density_model),
    };
}

bool cloud_layer_weather_generation_equal(const CloudLayerWeatherPushConstants& lhs,
                                          const CloudLayerWeatherPushConstants& rhs) {
    return lhs.fronts == rhs.fronts && lhs.cells == rhs.cells && lhs.streaks == rhs.streaks &&
           lhs.cloud_style == rhs.cloud_style && lhs.density_model == rhs.density_model;
}


} // namespace cubey::render
