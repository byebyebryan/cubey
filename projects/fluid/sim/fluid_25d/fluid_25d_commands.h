#pragma once

#include "fluid_25d_config.h"
#include "fluid_25d_gpu_resources.h"
#include "fluid_25d_presentation.h"

#include <cubey/core/math.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/target.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {

class Fluid25DMotionMarkers;

// The numerical terrain fields retain their metre-scale heights. The product
// view uses a restrained vertical presentation scale so the bounded River V0
// catchment remains readable from one deterministic orbit.
inline constexpr float kFluid25DCatchmentHeightScale = 0.08F;
inline constexpr float kFluid25DNaturalFlowStudyHeightScale = 1.0F;
inline constexpr float kFluid25DNaturalFlowStudyHomePitch = -0.72F;
// The authored source-to-outlet explanation scene has a compact, deliberately
// varied terrain profile. It needs more render-only vertical relief than the
// broad River V0 overview, while keeping every numerical terrain height and
// solver quantity unchanged.
inline constexpr float kFluid25DSourceOutletDemoHeightScale = 0.65F;
inline constexpr float kFluid25DSourceOutletDemoHomePitch = -0.72F;
// The authored headwaters banks are under a metre high numerically. Exaggerate
// them only in the catchment vertex transform and frame the compact domain
// closer so the Y-shaped terrain reads without changing solver elevations.
inline constexpr float kFluid25DSustainedHeadwatersHeightScale = 4.0F;
inline constexpr float kFluid25DSustainedHeadwatersHomePitch = -0.70F;
inline constexpr float kFluid25DSustainedHeadwatersHomeDistanceScale = 0.80F;
// The imported mountain crop spans 7 km across and more than 700 m vertically.
// This render-only scale makes its valley and side relief legible from a single
// overview without changing the metres supplied to the solver.
inline constexpr float kFluid25DMountainSourceOutletHeightScale = 0.60F;
inline constexpr float kFluid25DMountainSourceOutletHomePitch = -0.55F;
inline constexpr float kFluid25DMountainSourceOutletHomeFovyRadians = 0.84F;
inline constexpr float kFluid25DCatchmentFarPlaneRelativeHeadroom = 0.05F;
inline constexpr float kFluid25DCatchmentFarPlaneMinimumHeadroomM = 32.0F;

struct Fluid25DCatchmentOrbitLimits {
    float minimum_distance_m;
    float maximum_distance_m;
};

// Camera-space depth of any terrain point is bounded by orbit distance plus
// its Euclidean distance from the orbit target. Use the largest legal orbit
// distance so every supported yaw, pitch, and zoom keeps the terrain in front
// of the far plane without widening the near plane used for shallow-water
// depth precision.
[[nodiscard]] inline float fluid_25d_catchment_far_plane(float maximum_orbit_distance_m,
                                                         cubey::math::Vec3 terrain_minimum,
                                                         cubey::math::Vec3 terrain_maximum,
                                                         cubey::math::Vec3 target) {
    const auto finite = [](cubey::math::Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!std::isfinite(maximum_orbit_distance_m) || maximum_orbit_distance_m <= 0.0F ||
        !finite(terrain_minimum) || !finite(terrain_maximum) || !finite(target) ||
        terrain_minimum.x > terrain_maximum.x || terrain_minimum.y > terrain_maximum.y ||
        terrain_minimum.z > terrain_maximum.z) {
        throw std::invalid_argument("fluid 2.5D catchment camera bounds are invalid");
    }

    const float dx =
        std::max(std::abs(terrain_minimum.x - target.x), std::abs(terrain_maximum.x - target.x));
    const float dy =
        std::max(std::abs(terrain_minimum.y - target.y), std::abs(terrain_maximum.y - target.y));
    const float dz =
        std::max(std::abs(terrain_minimum.z - target.z), std::abs(terrain_maximum.z - target.z));
    const float terrain_radius_m = std::hypot(std::hypot(dx, dy), dz);
    const float orbit_and_terrain_m = maximum_orbit_distance_m + terrain_radius_m;
    const float headroom_m =
        std::max(kFluid25DCatchmentFarPlaneMinimumHeadroomM,
                 orbit_and_terrain_m * kFluid25DCatchmentFarPlaneRelativeHeadroom);
    const float far_plane_m = orbit_and_terrain_m + headroom_m;
    if (!std::isfinite(terrain_radius_m) || !std::isfinite(orbit_and_terrain_m) ||
        !std::isfinite(far_plane_m)) {
        throw std::invalid_argument("fluid 2.5D catchment camera bounds exceed finite range");
    }
    return far_plane_m;
}

[[nodiscard]] constexpr Fluid25DCatchmentOrbitLimits
fluid_25d_catchment_orbit_limits(Fluid25DScenario scenario, float domain_extent,
                                 float framing_extent) {
    // Camera-only hillside framing switches must not invalidate an explicit
    // home distance that was accepted at startup in the other view.
    const bool hillside = fluid_25d_is_macro_hillside_study(scenario);
    const float minimum_extent = hillside ? std::min(domain_extent, 3200.0F) : framing_extent;
    const float maximum_extent = hillside ? domain_extent : framing_extent;
    return {std::max(8.0F, minimum_extent * 0.30F), std::max(48.0F, maximum_extent * 4.0F)};
}

[[nodiscard]] constexpr float fluid_25d_catchment_height_scale(Fluid25DScenario scenario) {
    return fluid_25d_is_natural_terrain_study(scenario) ||
                   scenario == Fluid25DScenario::HillsideRainStudy
               ? kFluid25DNaturalFlowStudyHeightScale
               : (scenario == Fluid25DScenario::SustainedHeadwatersDemo
                      ? kFluid25DSustainedHeadwatersHeightScale
                      : (scenario == Fluid25DScenario::SourceOutletDemo
                             ? kFluid25DSourceOutletDemoHeightScale
                             : (scenario == Fluid25DScenario::MountainSourceOutletDemo
                                    ? kFluid25DMountainSourceOutletHeightScale
                                    : kFluid25DCatchmentHeightScale)));
}

[[nodiscard]] constexpr float
fluid_25d_catchment_home_horizontal_extent(Fluid25DScenario scenario,
                                           float domain_horizontal_extent, float route_span) {
    return scenario == Fluid25DScenario::SourceOutletDemo
               ? (route_span * 1.25F)
               : (scenario == Fluid25DScenario::MountainSourceOutletDemo
                      ? (route_span * 1.18F)
                      : domain_horizontal_extent);
}

[[nodiscard]] constexpr float fluid_25d_catchment_home_pitch(float default_pitch,
                                                             Fluid25DScenario scenario) {
    return fluid_25d_is_natural_terrain_study(scenario) ||
                   scenario == Fluid25DScenario::HillsideRainStudy
               ? kFluid25DNaturalFlowStudyHomePitch
               : (scenario == Fluid25DScenario::SustainedHeadwatersDemo
                      ? kFluid25DSustainedHeadwatersHomePitch
                      : (scenario == Fluid25DScenario::SourceOutletDemo
                             ? kFluid25DSourceOutletDemoHomePitch
                             : (scenario == Fluid25DScenario::MountainSourceOutletDemo
                                    ? kFluid25DMountainSourceOutletHomePitch
                                    : default_pitch)));
}

// The full mountain route almost spans the crop. Its narrower overview fills
// a normal widescreen capture while retaining both endpoint rings.
[[nodiscard]] constexpr float fluid_25d_catchment_home_distance_scale(Fluid25DScenario scenario) {
    return fluid_25d_is_macro_hillside_study(scenario) ? 1.50F
           : scenario == Fluid25DScenario::SustainedHeadwatersDemo
               ? kFluid25DSustainedHeadwatersHomeDistanceScale
               : (scenario == Fluid25DScenario::MountainSourceOutletDemo ? 0.85F : 1.05F);
}

[[nodiscard]] constexpr float fluid_25d_catchment_home_fovy_radians(float default_fovy_radians,
                                                                    Fluid25DScenario scenario) {
    return scenario == Fluid25DScenario::MountainSourceOutletDemo
               ? kFluid25DMountainSourceOutletHomeFovyRadians
               : default_fovy_radians;
}

// The demonstration scene alone uses a modest terrain height/slope tint. It
// complements its render-only relief scale without changing shared material
// behavior, numerical terrain, or any solver data.
[[nodiscard]] constexpr float fluid_25d_catchment_terrain_material_cue(Fluid25DScenario scenario) {
    return fluid_25d_is_macro_hillside_study(scenario) ? 4.0F
           : scenario == Fluid25DScenario::SustainedHeadwatersDemo
               ? 3.0F
               : (scenario == Fluid25DScenario::SourceOutletDemo
                      ? 1.0F
                      : (scenario == Fluid25DScenario::MountainSourceOutletDemo ? 2.0F : 0.0F));
}

struct Fluid25DRenderCamera {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec3 position{0.0F, 0.0F, 0.0F};
};

enum class Fluid25DRenderTargetMode : std::uint8_t {
    Present,
    ColorAttachment,
};

// One forcing sample belongs to one public fixed solver step. Hydraulic source
// scale and dye concentration deliberately travel together at the API boundary
// while remaining independent values in the solver contract.
struct Fluid25DStepForcing {
    float source_rate_scale = 1.0F;
    float dye_source_concentration = 0.0F;
};

// Explicitly presentation-only: never resets or advances imported hydraulic fields.
void record_fluid_25d_recorded_presentation(VkCommandBuffer command_buffer,
                                            Fluid25DGpuResources& resources,
                                            const Fluid25DConfig& config,
                                            float physical_delta_seconds, bool& cue_reset_requested,
                                            bool& quiver_reset_requested, bool show_quiver,
                                            bool native_motion = false);

void record_fluid_25d_compute(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                              const Fluid25DConfig& config, bool paused, bool& reset_requested,
                              bool include_render_visibility_barrier = true,
                              cubey::vulkan::GpuTimestampProfiler* profiler = nullptr,
                              std::uint32_t frame_slot_index = 0U,
                              Fluid25DStepForcing forcing = {});

// An opt-in presentation-only reset for headless Flow Inspection. Call this
// before the fixed solver record when a headless capture starts so its initial
// empty field cannot be mistaken for one completed flow step. It remains
// outside numerical profiler/oracle paths and is status-gated.
void record_fluid_25d_flow_inspection_quiver_reset(VkCommandBuffer command_buffer,
                                                   Fluid25DGpuResources& resources,
                                                   const Fluid25DConfig& config,
                                                   bool& quiver_reset_requested);

// An opt-in presentation-only step for headless Flow Inspection. Call it
// after the fixed solver step has returned so numerical profiler/oracle paths
// remain untouched; the implementation is status-gated and never reads back.
void record_fluid_25d_flow_inspection_quiver_step(VkCommandBuffer command_buffer,
                                                  Fluid25DGpuResources& resources,
                                                  const Fluid25DConfig& config,
                                                  bool& quiver_reset_requested);

void record_fluid_25d_compute_batch(VkCommandBuffer command_buffer, Fluid25DGpuResources& resources,
                                    const Fluid25DConfig& config,
                                    std::span<const Fluid25DStepForcing> forcings, bool paused,
                                    bool& reset_requested,
                                    bool include_render_visibility_barrier = true,
                                    cubey::vulkan::GpuTimestampProfiler* profiler = nullptr,
                                    std::uint32_t frame_slot_index = 0U);

void record_fluid_25d_fullscreen_draw(VkCommandBuffer command_buffer,
                                      const Fluid25DGpuResources& resources,
                                      const Fluid25DConfig& config, Fluid25DDebugView debug_view,
                                      cubey::render::ColorTargetView color_target,
                                      Fluid25DCatchmentRenderOptions render_options = {});

void record_fluid_25d_catchment_draw(
    VkCommandBuffer command_buffer, const Fluid25DGpuResources& resources,
    const Fluid25DConfig& config, Fluid25DCatchmentView catchment_view,
    const Fluid25DRenderCamera& camera, Fluid25DCatchmentRenderOptions render_options,
    cubey::render::ColorTargetView color_target, cubey::render::DepthTargetView depth_target,
    Fluid25DMotionMarkers* motion_markers = nullptr, float marker_interpolation = 1.0F);

[[nodiscard]] cubey::render::CompiledRenderGraph build_fluid_25d_frame_graph(
    cubey::render::ColorTargetView color_target, Fluid25DGpuResources& resources,
    const Fluid25DConfig& config, Fluid25DPresentationView presentation_view,
    Fluid25DCatchmentView catchment_view, Fluid25DDebugView debug_view,
    const Fluid25DRenderCamera& camera, Fluid25DRenderTargetMode target_mode,
    bool include_simulation, bool paused, bool& reset_requested,
    bool& presentation_cue_reset_requested, bool& quiver_reset_requested,
    cubey::vulkan::GpuTimestampProfiler* profiler = nullptr, std::uint32_t frame_slot_index = 0U,
    std::span<const Fluid25DStepForcing> forcings = {},
    Fluid25DCatchmentRenderOptions render_options = {},
    Fluid25DMotionMarkers* motion_markers = nullptr, float marker_interpolation = 1.0F,
    bool show_motion_markers = true);

} // namespace cubey::projects::fluid::fluid_25d
