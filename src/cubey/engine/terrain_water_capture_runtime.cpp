#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cubey/asset/file_digest.h>
#include <cubey/engine/atmosphere_environment_runtime.h>
#include <cubey/engine/terrain_surface_patch_scene.h>
#include <cubey/engine/terrain_water_capture_runtime.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/terrain_surface_patch_whitewater.h>
#include <cubey/terrain/terrain_surface_field.h>
#include <cubey/vulkan/buffer.h>
#include <stdexcept>

namespace cubey {
struct TerrainWaterCaptureRuntime::State {
    asset::TerrainWaterCapture capture;
    asset::TerrainRasterHeightSource source;
    std::filesystem::path shaders;
    TerrainSurfacePatchScene scene;
    render::RenderGraphFrameExecutor graph;
    AtmosphereEnvironmentRuntime daylight;
    std::optional<render::AtmosphereBackgroundAtlasResources> atlases;
    std::optional<vulkan::Buffer> bed, depth, velocity, zeros, coverage;
    std::optional<vulkan::DescriptorSetBundle> fields;
    std::optional<vulkan::GpuTimestampProfiler> whole_scene_profiler;
    std::vector<math::Vec4> velocity_values, weights;
    render::TerrainSurfacePatchWhitewaterSelection whitewater;
    render::TerrainSurfacePatchMaterial material = render::terrain_surface_patch_macro_material();
    State(const std::filesystem::path& capture_path, const std::filesystem::path& heightfield,
          std::filesystem::path directory)
        : capture(asset::load_terrain_water_capture(capture_path)), source(heightfield),
          shaders(std::move(directory)), scene(shaders) {
        asset::validate_terrain_water_capture_source(capture, source);
        std::optional<asset::TerrainRasterClimateSource> climate;
        const auto companion =
            (std::filesystem::is_directory(heightfield) ? heightfield : heightfield.parent_path()) /
            "surface-fields.json";
        if (std::filesystem::exists(companion)) {
            climate.emplace(companion);
            asset::validate_terrain_climate_binding(source, *climate);
        }
        const auto field = terrain::make_terrain_surface_field(source, source.bounds(),
                                                               climate ? &*climate : nullptr);
        const auto& g = capture.grid;
        weights.reserve(capture.bed_m.size());
        velocity_values.reserve(capture.bed_m.size());
        for (std::uint32_t z = 0; z < g.height; ++z)
            for (std::uint32_t x = 0; x < g.width; ++x) {
                weights.push_back(field.sample(g.sample_origin_xz_m +
                                               math::Vec2{float(x), float(z)} * g.cell_size_m));
                const auto v = capture.velocity_xz_m_per_s[std::size_t(z) * g.width + x];
                velocity_values.push_back(
                    {v.x, v.y, capture.depth_m[std::size_t(z) * g.width + x] > 0.0F ? 1.0F : 0.0F,
                     0.0F});
            }
        whitewater = render::terrain_surface_patch_whitewater_seeds(
            g.width, g.height, g.cell_size_m, std::span<const float>{capture.bed_m},
            std::span<const float>{capture.depth_m},
            std::span<const math::Vec2>{capture.velocity_xz_m_per_s},
            std::uint32_t(material.water_whitewater_budget));
    }
    render::TerrainSurfacePatchFields field_view() const {
        return {*bed, *depth, *velocity, *coverage, fields->set()};
    }
    render::TerrainSurfacePatchGrid grid() const {
        return {capture.grid.width, capture.grid.height, capture.grid.cell_size_m, 0.0001F};
    }
};
TerrainWaterCaptureRuntime::TerrainWaterCaptureRuntime(const std::filesystem::path& capture,
                                                       const std::filesystem::path& heightfield,
                                                       std::filesystem::path shaders) {
    const auto started = std::chrono::steady_clock::now();
    state_ = std::make_unique<State>(capture, heightfield, std::move(shaders));
    std::printf(
        "terrain_water_capture: bound rain-fed state %.6f s, %.3f mm/h, %ux%u / %.3f m; "
        "load_and_appearance_ms=%.3f; hydraulic_dispatches=0\n",
        state_->capture.capture_time_s, state_->capture.rain_mm_per_hour,
        state_->capture.grid.width, state_->capture.grid.height, state_->capture.grid.cell_size_m,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count());
}
TerrainWaterCaptureRuntime::~TerrainWaterCaptureRuntime() = default;
void TerrainWaterCaptureRuntime::ensure_resources(const vulkan::Device& device,
                                                  vulkan::GpuRuntime& gpu, std::uint32_t slots,
                                                  render::ColorTargetView target) {
    auto& s = *state_;
    if (s.graph.frame_slot_count() != slots)
        s.graph.resize(slots);
    if (!s.fields) {
        const auto started = std::chrono::steady_clock::now();
        const auto upload = [&](const void* data, VkDeviceSize bytes) {
            return vulkan::upload_device_buffer(gpu, data, bytes,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        };
        const auto bytes = s.capture.bed_m.size() * sizeof(float);
        s.bed.emplace(upload(s.capture.bed_m.data(), bytes));
        s.depth.emplace(upload(s.capture.depth_m.data(), bytes));
        s.velocity.emplace(
            upload(s.velocity_values.data(), s.velocity_values.size() * sizeof(math::Vec4)));
        const std::vector<float> zero(s.capture.bed_m.size(), 0.0F);
        s.zeros.emplace(upload(zero.data(), bytes));
        const std::array<float, 5> coverage{1.0F, 1.0F, 0.0F, 0.0F, 1.0F};
        s.coverage.emplace(upload(coverage.data(), sizeof(coverage)));
        std::vector<vulkan::DescriptorSetBindingConfig> bindings;
        for (std::uint32_t i = 0; i <= 8U; ++i)
            bindings.push_back(
                {.binding = i,
                 .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT});
        s.fields.emplace(device, vulkan::DescriptorSetInfo(bindings));
        render::MaterialDescriptorWriter writer(s.fields->set());
        for (std::uint32_t i = 0; i <= 8U; ++i) {
            const auto& b = i == 0U   ? *s.bed
                            : i == 1U ? *s.depth
                            : i == 2U ? *s.velocity
                            : i == 8U ? *s.coverage
                                      : *s.zeros;
            writer.storage_buffer(i, b.handle(), b.size());
        }
        writer.update(device);
        s.whole_scene_profiler.emplace(device, slots, 1U);
        std::printf(
            "terrain_water_capture_upload: once; retained_input_gpu_bytes=%llu wall_ms=%.3f\n",
            static_cast<unsigned long long>(retained_gpu_field_bytes()),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count());
    }
    const render::TerrainSurfacePatchSurface surface{
        s.capture.grid.width, s.capture.grid.height, {}, {}, s.weights};
    s.scene.ensure_resources(device, gpu, slots, target, s.grid(), s.capture.bed_m, s.field_view(),
                             true, &surface, true);
    if (!s.daylight.resources_created()) {
        s.atlases.emplace(render::create_atmosphere_background_placeholder_textures(device, gpu));
        s.daylight.create_resources(device, {.reflection_extent = 64U,
                                             .reflection_mip_levels = 5U,
                                             .frame_slot_count = slots,
                                             .atmosphere_textures = s.atlases->bindings()});
        s.daylight.create_pipelines(
            device, {.atmosphere_vertex_shader = s.shaders / "atmosphere.vert.spv",
                     .atmosphere_fragment_shader = s.shaders / "atmosphere.frag.spv",
                     .reflection_prefilter_vertex_shader = s.shaders / "atmosphere.vert.spv",
                     .reflection_prefilter_fragment_shader =
                         s.shaders / "atmosphere_reflection_prefilter.frag.spv"});
        s.daylight.set_environment(render::terrain_surface_patch_fixed_daylight());
    }
}
void TerrainWaterCaptureRuntime::record(const vulkan::Device& device, VkCommandBuffer commands,
                                        render::FrameSlot slot, render::ColorTargetView target,
                                        bool present,
                                        const render::TerrainSurfacePatchCamera& camera,
                                        double seconds, bool water, bool profile) {
    auto& s = *state_;
    auto* profiler = profile ? &*s.whole_scene_profiler : nullptr;
    if (profiler)
        profiler->begin_frame(commands, slot.index);
    vulkan::GpuTimestampScope whole_scene(profiler, commands, slot.index,
                                          "terrain captured patch whole scene");
    s.daylight.record_pending_update(vulkan::CommandRecorder(commands), slot);
    const render::TerrainSurfacePatchEnvironment environment{
        s.daylight.pbr_environment_bindings(s.scene.fallback_environment()),
        s.daylight.lighting(),
        s.material.daylight_exposure,
        s.daylight.reflection_probe().sky_radiance_cube().sampler().handle(),
        s.daylight.reflection_probe().sky_radiance_cube().view(),
        render::atmosphere_environment_frame_uniforms(s.daylight.environment(), {})};
    s.scene.record(device, commands, s.graph, slot, target, present, s.field_view(), s.grid(),
                   camera, {.terrain_thin_water_composite = true, .water_visible = water},
                   seconds * 0.03, s.material, {}, profile, false, 0U, 0U, 3U, &environment,
                   {0U, seconds, seconds, s.capture.rain_mm_per_hour}, s.whitewater.seeds, 0U);
}
void TerrainWaterCaptureRuntime::destroy_target_resources() {
    if (state_) {
        state_->scene.destroy_swapchain_resources();
        state_->graph.clear();
    }
}
void TerrainWaterCaptureRuntime::destroy() {
    state_.reset();
}
std::vector<vulkan::GpuPassTiming> TerrainWaterCaptureRuntime::collect_timings(std::uint32_t slot) {
    auto timings = state_->scene.collect_timings(slot);
    if (state_->whole_scene_profiler) {
        state_->whole_scene_profiler->collect(slot);
        const auto& whole = state_->whole_scene_profiler->latest_timings();
        timings.insert(timings.end(), whole.begin(), whole.end());
    }
    return timings;
}
const asset::TerrainWaterCapture& TerrainWaterCaptureRuntime::capture() const {
    return state_->capture;
}
std::string TerrainWaterCaptureRuntime::source_label() const {
    return std::string(state_->source.metadata().id);
}
VkDeviceSize TerrainWaterCaptureRuntime::retained_gpu_field_bytes() const {
    const auto& s = *state_;
    return s.bed ? s.bed->size() + s.depth->size() + s.velocity->size() + s.zeros->size() +
                       s.coverage->size()
                 : 0U;
}
void TerrainWaterCaptureRuntime::verify_immutable_fields(vulkan::GpuRuntime& gpu) const {
    const auto& s = *state_;
    if (!s.fields)
        return;
    for (const auto& [buffer, expected] :
         std::array<std::pair<const vulkan::Buffer*, std::span<const std::byte>>, 3>{
             {{&*s.bed, std::as_bytes(std::span{s.capture.bed_m})},
              {&*s.depth, std::as_bytes(std::span{s.capture.depth_m})},
              {&*s.velocity, std::as_bytes(std::span{s.velocity_values})}}}) {
        std::vector<std::byte> actual(expected.size());
        // Explicitly outside frame profiling and after the caller's retirement wait.
        (void)gpu.submit_and_wait(
            {.label = "captured water immutable readback", .work = [&](auto& context) {
                 vulkan::Buffer readback(context.device(),
                                         vulkan::readback_buffer_config(buffer->size()));
                 vulkan::copy_buffer(context, buffer->handle(), readback.handle(), buffer->size());
                 readback.download(actual.data(), actual.size());
             }});
        if (asset::sha256_hex(actual) != asset::sha256_hex(expected))
            throw std::runtime_error("captured water immutable GPU fields changed");
    }
    std::printf("terrain_water_capture_immutable: GPU bed/depth/decoded-velocity exact; "
                "hydraulic_dispatches=0; no radial_ground; clouds_disabled\n");
}
} // namespace cubey
