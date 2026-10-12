#include "fluid_25d_scenic.h"
#include "fluid_25d_motion_markers.h"
#include <cubey/engine/terrain_surface_patch_scene.h>

namespace cubey::projects::fluid::fluid_25d {
namespace {
render::TerrainSurfacePatchGrid grid(const Fluid25DConfig& c) {
    return {c.grid_width, c.grid_height, c.cell_size_m, c.minimum_wet_depth_m};
}
render::TerrainSurfacePatchFields fields(const Fluid25DGpuResources& f) {
    return {f.terrain(), f.depth_a(), f.velocity(), f.display_coverage(),
            f.render_descriptor_set()};
}
render::TerrainSurfacePatchOptions options(const Fluid25DCatchmentRenderOptions& o) {
    return {.terrain_height_scale = o.terrain_height_scale,
            .terrain_thin_water_composite = o.terrain_thin_water_composite,
            .native_bilinear_water = o.native_bilinear_water,
            .native_bspline_surface = o.native_bspline_surface,
            .native_display_coverage = o.native_display_coverage,
            .native_surface_subdivision = o.native_surface_subdivision,
            .native_shadow_scene_fit = o.native_shadow_scene_fit,
            .native_shadow_extent = o.native_shadow_extent};
}
} // namespace
struct Fluid25DScenic::State {
    ::cubey::TerrainSurfacePatchScene scene{CUBEY_FLUID_25D_SHADER_DIR};
};
Fluid25DScenic::Fluid25DScenic() = default;
Fluid25DScenic::~Fluid25DScenic() = default;
bool Fluid25DScenic::ensure_resources(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                      std::uint32_t slots, render::ColorTargetView target,
                                      const Fluid25DConfig& c, const Fluid25DScenarioData& scenario,
                                      const Fluid25DGpuResources& f, bool integrated,
                                      const Fluid25DTerrainSurface* surface, bool daylight) {
    if (!state_)
        state_ = std::make_unique<State>();
    std::optional<render::TerrainSurfacePatchSurface> weights;
    if (surface)
        weights = {surface->width, surface->height, surface->landform, surface->climate,
                   surface->correlated};
    return state_->scene.ensure_resources(device, gpu, slots, target, grid(c),
                                          scenario.terrain_height_m, fields(f), integrated,
                                          weights ? &*weights : nullptr, daylight);
}
void Fluid25DScenic::destroy_swapchain_resources() {
    if (state_)
        state_->scene.destroy_swapchain_resources();
}
void Fluid25DScenic::destroy() {
    state_.reset();
}
const render::GeneratedPbrEnvironment& Fluid25DScenic::fallback_environment() const {
    if (!state_)
        throw std::runtime_error("Scenic fallback environment is not initialized");
    return state_->scene.fallback_environment();
}
std::vector<vulkan::GpuPassTiming> Fluid25DScenic::collect_timings(std::uint32_t slot) {
    return state_ ? state_->scene.collect_timings(slot) : std::vector<vulkan::GpuPassTiming>{};
}
void Fluid25DScenic::record(
    vulkan::Device& device, VkCommandBuffer commands, render::RenderGraphFrameExecutor& executor,
    render::FrameSlot slot, render::ColorTargetView target, Fluid25DRenderTargetMode mode,
    const Fluid25DGpuResources& f, const Fluid25DConfig& c, const Fluid25DRenderCamera& camera,
    Fluid25DCatchmentRenderOptions o, double clock, const Fluid25DScenicMaterial& material,
    Fluid25DMotionMarkers* markers, float fraction, bool profile, bool reset, unsigned terrain_view,
    unsigned water_view, unsigned surface_mode, const Fluid25DScenicEnvironment* environment,
    Fluid25DRainVisualFrame rain, std::span<const Fluid25DWhitewaterSeed> seeds,
    std::uint64_t generation) {
    const auto overlay = [&](const vulkan::CommandRecorder& r) {
        if (markers)
            markers->record_draw(r.handle(), f.current_depth_is_a(), camera.view_projection,
                                 target.extent, o.terrain_height_scale.value_or(1.0F), fraction,
                                 o.native_bspline_surface || o.native_bilinear_water,
                                 o.native_bspline_surface ? o.native_surface_subdivision : 0U);
    };
    state_->scene.record(
        device, commands, executor, slot, target, mode == Fluid25DRenderTargetMode::Present,
        fields(f), grid(c), {camera.view_projection, camera.position}, options(o), clock, material,
        overlay, profile, reset, terrain_view, water_view, surface_mode, environment,
        {rain.streak_count, rain.clock_s, rain.motion_s, rain.applied_mm_per_hour}, seeds,
        generation);
}
} // namespace cubey::projects::fluid::fluid_25d
