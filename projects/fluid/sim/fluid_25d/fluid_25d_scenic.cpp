#include "fluid_25d_scenic.h"
#include "fluid_25d_motion_markers.h"

#include <cubey/render/generated_ibl.h>
#include <cubey/render/generated_texture.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/pass.h>
#include <cubey/render/shadow_map.h>
#include <cubey/vulkan/memory_barriers.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {
namespace {
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkExtent2D kShadowExtent{2048U, 2048U};
struct Push {
    math::Mat4 view_projection;
    math::Vec4 grid_cell, camera_wet, presentation, terrain_palette;
};
struct Uniforms {
    math::Mat4 inverse_view_projection, shadow_view_projection;
    math::Vec4 light_direction_exposure, light_color_mips, clock_encoding;
    math::Vec4 ground_material, surface_material, water_optics, art_direction;
    math::Vec4 terrain_macro;
    math::Vec4 water_film, water_view;
    math::Vec4 terrain_surface;
    math::Vec4 environment_mode;
    math::Vec4 water_shallow_optics;
};
struct DaylightPush {
    math::Vec4 camera_position_radius, radii_ground, rayleigh, mie, ozone;
    math::Vec4 sun_direction_radius, atmosphere_options;
};
static_assert(sizeof(DaylightPush) == 112U);
static_assert(sizeof(Push) == 128U);
static_assert(sizeof(Uniforms) == 336U);
static_assert(offsetof(Uniforms, terrain_macro) == 240U);
static_assert(offsetof(Uniforms, water_view) == 272U);
static_assert(offsetof(Uniforms, terrain_surface) == 288U);
static_assert(offsetof(Uniforms, environment_mode) == 304U);
static_assert(offsetof(Uniforms, water_shallow_optics) == 320U);
std::filesystem::path shader(const char* name) {
    return std::filesystem::path(CUBEY_FLUID_25D_SHADER_DIR) / name;
}
render::MaterialPassInfo frame_material() {
    render::MaterialPassInfo result{.label = "fluid_25d.scenic.frame"};
    result.descriptor_sets.push_back({.set = 1U});
    auto& bindings = result.descriptor_sets.back().bindings;
    bindings.push_back({.binding = 0U,
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT});
    for (std::uint32_t i = 1; i <= 8; ++i)
        bindings.push_back({.binding = i,
                            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT});
    bindings.push_back({.binding = 9U,
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT});
    bindings.push_back({.binding = 10U,
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT});
    for (std::uint32_t i = 11U; i <= 13U; ++i)
        bindings.push_back({.binding = i,
                            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT});
    bindings.push_back({.binding = 14U,
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT});
    return result;
}
render::MaterialPassInfo mesh_pass(bool water = false) {
    render::MaterialPassInfo result{.label = "fluid_25d.scenic.mesh"};
    result.push_constants.push_back(
        {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
         .offset = 0U,
         .size = sizeof(Push)});
    result.depth_test = !water;
    result.depth_write = !water;
    result.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
    result.blend_enable = water;
    result.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
    result.dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    result.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
    result.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    return result;
}
render::RenderGraphTextureDesc texture(const char* name, VkExtent2D extent, VkFormat format,
                                       bool depth = false) {
    return {.label = name,
            .extent = {extent.width, extent.height, 1U},
            .format = format,
            .aspects = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT};
}
bool srgb_attachment(VkFormat format) {
    return format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB;
}
render::Texture2D upload_surface(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                 std::uint32_t width, std::uint32_t height,
                                 std::vector<math::Vec4> values) {
    const VkExtent2D extent{width, height};
    const auto levels = render::texture_2d_mip_count(extent);
    std::vector<std::uint8_t> bytes;
    for (unsigned level = 0; level < levels; ++level) {
        for (const auto& value : values)
            for (int c = 0; c < 4; ++c)
                bytes.push_back(static_cast<std::uint8_t>(
                    std::lround(std::clamp(value[c], 0.0F, 1.0F) * 255.0F)));
        if (level + 1U == levels)
            break;
        const unsigned next_width = std::max(1U, width / 2U);
        const unsigned next_height = std::max(1U, height / 2U);
        std::vector<math::Vec4> next(std::size_t(next_width) * next_height);
        for (unsigned z = 0; z < next_height; ++z)
            for (unsigned x = 0; x < next_width; ++x) {
                math::Vec4 sum{0.0F};
                // Area average includes the final row/column for odd dimensions.
                const unsigned x0 = x * width / next_width, x1 = (x + 1U) * width / next_width;
                const unsigned z0 = z * height / next_height, z1 = (z + 1U) * height / next_height;
                for (unsigned zz = z0; zz < z1; ++zz)
                    for (unsigned xx = x0; xx < x1; ++xx)
                        sum += values[std::size_t(zz) * width + xx];
                next[std::size_t(z) * next_width + x] = sum / float((x1 - x0) * (z1 - z0));
            }
        values = std::move(next);
        width = next_width;
        height = next_height;
    }
    return render::create_uploaded_texture_2d(
        device, gpu,
        {.extent = extent,
         .mip_levels = levels,
         .format = VK_FORMAT_R8G8B8A8_UNORM,
         .bytes = bytes,
         .sampler = {.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                     .max_lod = float(levels - 1U)}});
}
void draw(const vulkan::CommandRecorder& recorder, const render::GraphicsPipelineResource& pipeline,
          VkDescriptorSet fields, VkDescriptorSet frame, const Push& push, std::uint32_t count) {
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0U, fields);
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 1U, frame);
    recorder.push_constants(pipeline.layout(),
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U, push);
    recorder.draw(count);
}
} // namespace

struct Fluid25DScenic::State {
    std::optional<render::GeneratedPbrEnvironment> environment;
    std::optional<render::TextureCube> terrain_diffuse;
    std::optional<render::Texture2D> detail;
    std::optional<render::Texture2D> landform_surface, climate_surface;
    std::optional<render::Texture2D> correlated_surface;
    std::optional<vulkan::Sampler> linear_sampler, nearest_sampler;
    std::optional<render::FrameUniformMaterialInstance<Uniforms>> frame;
    std::optional<vulkan::GpuTimestampProfiler> profiler;
    std::optional<vulkan::Buffer> visual_flow;
    std::optional<vulkan::DescriptorSetBundle> visual_flow_set;
    std::optional<render::ComputePipelineResource> visual_flow_pipeline;
    double previous_clock_s = 0.0;
    bool flow_initialized = false;
    std::optional<vulkan::Buffer> captured_daylight;
    std::optional<vulkan::DescriptorSetBundle> daylight_set;
    std::optional<render::ComputePipelineResource> daylight_pipeline;
    bool daylight_initialized = false;
    std::optional<render::GraphicsPipelineResource> terrain, terrain_bspline, water, water_bspline;
    std::optional<render::GraphicsPipelineResource> terrain_legacy, terrain_bspline_legacy;
    std::optional<render::GraphicsPipelineResource> shadow, shadow_bspline, sky, copy, display;
    math::Mat4 shadow_matrix{1.0F};
    float terrain_low = 0.0F, terrain_high = 0.0F, domain_m = 0.0F;
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
};
Fluid25DScenic::Fluid25DScenic() = default;
Fluid25DScenic::~Fluid25DScenic() = default;

bool Fluid25DScenic::ensure_resources(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                      std::uint32_t slots, render::ColorTargetView target,
                                      const Fluid25DConfig& config,
                                      const Fluid25DScenarioData& scenario,
                                      const Fluid25DGpuResources& fields,
                                      bool integrated_terrain_diffuse,
                                      const Fluid25DTerrainSurface* surface,
                                      bool captured_daylight) {
    bool diffuse_created = false;
    if (!state_) {
        state_ = std::make_unique<State>();
        auto& s = *state_;
        // Existing deterministic Cubey environment, no asset or atmosphere service dependency.
        s.environment.emplace(render::create_generated_pbr_environment(device, gpu));
        const std::uint32_t seed = 0x25d2026U;
        s.detail.emplace(render::create_compute_generated_texture_2d(
            device, gpu,
            {
                .label = "fluid_25d scenic filtered terrain detail",
                .extent = {512U, 512U},
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .mip_levels = 10U,
                .shader = render::compute_shader_file(shader("terrain_backdrop_material.comp.spv")),
                .group_size_x = 8U,
                .group_size_y = 8U,
                .push_constants = std::as_bytes(std::span{&seed, 1U}),
                .sampler = {.address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT, .max_lod = 9.0F},
            }));
        s.linear_sampler.emplace(
            device, vulkan::SamplerConfig{.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE});
        s.nearest_sampler.emplace(
            device, vulkan::SamplerConfig{.min_filter = VK_FILTER_NEAREST,
                                          .mag_filter = VK_FILTER_NEAREST,
                                          .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE});
        s.frame.emplace(
            device, render::FrameUniformMaterialInstanceConfig{.material_pass = frame_material(),
                                                               .descriptor_set = 1U,
                                                               .frame_slot_count = slots});
        s.profiler.emplace(device, slots, 5U);
        s.visual_flow.emplace(
            device, vulkan::device_local_buffer_config(fields.velocity().size(),
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        const std::array<vulkan::DescriptorSetBindingConfig, 2> flow_bindings{
            {{.binding = 0U,
              .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
             {.binding = 1U,
              .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT}}};
        s.visual_flow_set.emplace(device, vulkan::DescriptorSetInfo(flow_bindings));
        render::MaterialDescriptorWriter flow_writer(s.visual_flow_set->set());
        flow_writer.storage_buffer(0U, fields.velocity().handle(), fields.velocity().size())
            .storage_buffer(1U, s.visual_flow->handle(), s.visual_flow->size())
            .update(device);
        const std::array<VkPushConstantRange, 1> flow_push{
            {{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0U, .size = 16U}}};
        render::emplace_single_set_compute_pipeline_resource(
            s.visual_flow_pipeline, device,
            render::compute_shader_file(shader("fluid_25d_scenic_flow.comp.spv")),
            s.visual_flow_set->layout(), flow_push);
        const auto [lo, hi] =
            std::minmax_element(scenario.terrain_height_m.begin(), scenario.terrain_height_m.end());
        s.terrain_low = *lo;
        s.terrain_high = *hi;
        s.domain_m = static_cast<float>(std::max(config.grid_width - 1U, config.grid_height - 1U)) *
                     config.cell_size_m;
    }
    auto& s = *state_;
    if (captured_daylight && !s.captured_daylight) {
        s.captured_daylight.emplace(
            device, vulkan::device_local_buffer_config(sizeof(math::Vec4) * 10U,
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        const std::array<vulkan::DescriptorSetBindingConfig, 2> bindings{
            {{.binding = 0U,
              .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
             {.binding = 1U,
              .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT}}};
        s.daylight_set.emplace(device, vulkan::DescriptorSetInfo(bindings));
        const std::array<VkPushConstantRange, 1> push{{{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                        .offset = 0U,
                                                        .size = sizeof(DaylightPush)}}};
        render::emplace_single_set_compute_pipeline_resource(
            s.daylight_pipeline, device,
            render::compute_shader_file(shader("fluid_25d_daylight.comp.spv")),
            s.daylight_set->layout(), push);
        diffuse_created = true;
    }
    if (surface && !s.landform_surface) {
        s.landform_surface.emplace(
            upload_surface(device, gpu, surface->width, surface->height, surface->landform));
        s.climate_surface.emplace(
            upload_surface(device, gpu, surface->width, surface->height, surface->climate));
        if (!surface->correlated.empty())
            s.correlated_surface.emplace(
                upload_surface(device, gpu, surface->width, surface->height, surface->correlated));
        diffuse_created = true;
    }
    if (integrated_terrain_diffuse && !s.terrain_diffuse) {
        const auto bytes = render::generate_generated_diffuse_irradiance();
        s.terrain_diffuse.emplace(render::create_uploaded_texture_cube(
            device, gpu,
            {.extent = 32U,
             .mip_levels = 1U,
             .format = VK_FORMAT_R32G32B32A32_SFLOAT,
             .bytes = bytes,
             .create_sampler = true,
             .sampler = {.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE}}));
        diffuse_created = true;
        std::printf("fluid_25d_terrain_diffuse: cosine-integrated E/pi; 32x32x6; 256 samples; "
                    "water environment unchanged\n");
        std::fflush(stdout);
    }
    if (s.terrain && s.extent.width == target.extent.width &&
        s.extent.height == target.extent.height && s.format == target.format)
        return diffuse_created;
    destroy_swapchain_resources();
    s.extent = target.extent;
    s.format = target.format;
    std::printf("fluid_25d_scenic_resources: extent=%ux%u HDR=rgba16f shadow=2048x2048\n",
                target.extent.width, target.extent.height);
    std::fflush(stdout);
    // Set zero matches the immutable native field layout; set one owns frame-slot textures.
    // The actual field layout is supplied below by an equivalent storage-buffer schema.
    std::vector<vulkan::DescriptorSetBindingConfig> bindings;
    for (std::uint32_t i = 0; i <= 8U; ++i)
        bindings.push_back(
            {.binding = i,
             .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT});
    vulkan::DescriptorSetBundle field_layout(device, vulkan::DescriptorSetInfo(bindings));
    const std::array<VkDescriptorSetLayout, 2> layouts{field_layout.layout(), s.frame->layout()};
    const auto mesh = [&](std::optional<render::GraphicsPipelineResource>& p, const char* vertex,
                          const char* fragment, bool water) {
        const std::array<render::ShaderStageFile, 2> stages{
            render::vertex_shader_file(shader(vertex)),
            render::fragment_shader_file(shader(fragment))};
        p.emplace(device, render::GraphicsPipelineFileResourceConfig{
                              .extent = target.extent,
                              .color_format = kHdrFormat,
                              .depth_format = water ? VK_FORMAT_UNDEFINED : VK_FORMAT_D32_SFLOAT,
                              .shader_stage_files = stages,
                              .descriptor_set_layouts = layouts,
                              .material_pass = mesh_pass(water)});
    };
    mesh(s.terrain, "fluid_25d_terrain.vert.spv", "fluid_25d_scenic_terrain.frag.spv", false);
    mesh(s.terrain_bspline, "fluid_25d_terrain_bspline.vert.spv",
         "fluid_25d_scenic_terrain.frag.spv", false);
    mesh(s.terrain_legacy, "fluid_25d_terrain.vert.spv", "fluid_25d_scenic_terrain_legacy.frag.spv",
         false);
    mesh(s.terrain_bspline_legacy, "fluid_25d_terrain_bspline.vert.spv",
         "fluid_25d_scenic_terrain_legacy.frag.spv", false);
    mesh(s.water, "fluid_25d_water.vert.spv", "fluid_25d_scenic_water.frag.spv", true);
    mesh(s.water_bspline, "fluid_25d_water_bspline.vert.spv", "fluid_25d_scenic_water.frag.spv",
         true);
    const auto shadow = [&](std::optional<render::GraphicsPipelineResource>& p,
                            const char* vertex) {
        const std::array<render::ShaderStageFile, 1> stages{
            render::vertex_shader_file(shader(vertex))};
        auto pass = mesh_pass();
        pass.kind = render::MaterialPassKind::DepthOnly;
        p.emplace(device,
                  render::GraphicsPipelineFileResourceConfig{.extent = kShadowExtent,
                                                             .color_format = VK_FORMAT_UNDEFINED,
                                                             .depth_format = VK_FORMAT_D32_SFLOAT,
                                                             .shader_stage_files = stages,
                                                             .descriptor_set_layouts = layouts,
                                                             .material_pass = pass});
    };
    shadow(s.shadow, "fluid_25d_terrain.vert.spv");
    shadow(s.shadow_bspline, "fluid_25d_terrain_bspline.vert.spv");
    const auto fullscreen = [&](std::optional<render::GraphicsPipelineResource>& p,
                                const char* fragment, VkFormat format, bool depth) {
        const std::array<render::ShaderStageFile, 2> stages{
            render::vertex_shader_file(shader("fluid_25d.vert.spv")),
            render::fragment_shader_file(shader(fragment))};
        p.emplace(device, render::GraphicsPipelineFileResourceConfig{
                              .extent = target.extent,
                              .color_format = format,
                              .depth_format = depth ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED,
                              .shader_stage_files = stages,
                              .descriptor_set_layouts = layouts,
                              .material_pass = {.label = "fluid_25d.scenic.fullscreen"}});
    };
    fullscreen(s.sky, "fluid_25d_scenic_sky.frag.spv", kHdrFormat, true);
    fullscreen(s.copy, "fluid_25d_scenic_copy.frag.spv", kHdrFormat, false);
    fullscreen(s.display, "fluid_25d_scenic_display.frag.spv", target.format, true);
    return true;
}

void Fluid25DScenic::destroy_swapchain_resources() {
    if (!state_)
        return;
    auto& s = *state_;
    s.display.reset();
    s.copy.reset();
    s.sky.reset();
    s.shadow_bspline.reset();
    s.shadow.reset();
    s.water_bspline.reset();
    s.water.reset();
    s.terrain_bspline.reset();
    s.terrain.reset();
    s.terrain_bspline_legacy.reset();
    s.terrain_legacy.reset();
}
void Fluid25DScenic::destroy() {
    state_.reset();
}
const render::GeneratedPbrEnvironment& Fluid25DScenic::fallback_environment() const {
    if (!state_ || !state_->environment)
        throw std::runtime_error("Scenic fallback environment is not initialized");
    return *state_->environment;
}
std::vector<vulkan::GpuPassTiming> Fluid25DScenic::collect_timings(std::uint32_t slot) {
    if (!state_)
        return {};
    state_->profiler->collect(slot);
    return state_->profiler->latest_timings();
}

void Fluid25DScenic::record(vulkan::Device& device, VkCommandBuffer commands,
                            render::RenderGraphFrameExecutor& executor, render::FrameSlot slot,
                            render::ColorTargetView target, Fluid25DRenderTargetMode target_mode,
                            const Fluid25DGpuResources& resources, const Fluid25DConfig& config,
                            const Fluid25DRenderCamera& camera,
                            Fluid25DCatchmentRenderOptions options, double visual_clock_s,
                            const Fluid25DScenicMaterial& material, Fluid25DMotionMarkers* markers,
                            float marker_fraction, bool profile, bool reset_visual_flow,
                            unsigned terrain_view, unsigned water_view, unsigned surface_mode,
                            const Fluid25DScenicEnvironment* environment) {
    auto& s = *state_;
    auto* profiler = profile ? &*s.profiler : nullptr;
    if (profiler)
        profiler->begin_frame(commands, slot.index);
    if (environment && !s.daylight_initialized) {
        if (!s.captured_daylight || !s.daylight_pipeline ||
            environment->sky_radiance_sampler == VK_NULL_HANDLE ||
            environment->sky_radiance_view == VK_NULL_HANDLE)
            throw std::runtime_error("Scenic captured daylight resources are not initialized");
        render::MaterialDescriptorWriter writer(s.daylight_set->set());
        writer
            .combined_image_sampler(0U, environment->sky_radiance_sampler,
                                    environment->sky_radiance_view)
            .storage_buffer(1U, s.captured_daylight->handle(), s.captured_daylight->size())
            .update(device);
        const auto& a = environment->atmosphere_frame;
        const DaylightPush push{
            a.camera_position_radius, a.radii_ground,      a.rayleigh, a.mie, a.ozone,
            a.sun_direction_radius,   a.atmosphere_options};
        const vulkan::CommandRecorder recorder(commands);
        // The probe's final layout is shader-read, but its built-in visibility
        // barrier targets fragment sampling. This one-time consumer is compute.
        const VkMemoryBarrier sky_ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                        .pNext = nullptr,
                                        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
        recorder.pipeline_barrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0U, {&sky_ready, 1U}, {},
                                  {});
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, s.daylight_pipeline->pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, s.daylight_pipeline->layout(),
                                     0U, s.daylight_set->set());
        recorder.push_constants(s.daylight_pipeline->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0U,
                                push);
        recorder.dispatch(1U, 1U, 1U);
        const VkBufferMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                            .pNext = nullptr,
                                            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .buffer = s.captured_daylight->handle(),
                                            .offset = 0U,
                                            .size = s.captured_daylight->size()};
        recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0U, {}, {&barrier, 1U},
                                  {});
        s.daylight_initialized = true;
        std::printf("fluid_25d_scenic_daylight: captured sky SH E/pi + atmosphere sun; "
                    "2048 sphere samples; once; no CPU readback\n");
        std::fflush(stdout);
    }
    const bool bspline = options.native_bspline_surface;
    const bool legacy_terrain =
        !environment && surface_mode == 0U && terrain_view == 0U &&
        material.terrain_material_blend == 0.0F && material.terrain_normal_strength < 0.0F &&
        material.terrain_ambient_softening == 0.0F &&
        material.terrain_diffuse_convolution == 0.0F && material.terrain_specular_scale == 1.0F &&
        material.terrain_shadow_scale == 1.0F && material.terrain_slope_color_scale == 1.0F;
    const auto subdivision = bspline ? options.native_surface_subdivision : 1U;
    const auto vertex_count = fluid_25d_mesh_vertex_count(config) * subdivision * subdivision;
    if (vertex_count > std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("Scenic mesh exceeds Vulkan draw vertex count");
    const auto vertices = static_cast<std::uint32_t>(vertex_count);
    const float height_scale = options.terrain_height_scale.value_or(1.0F);
    const float shadow_domain =
        std::max(s.domain_m, (s.terrain_high - s.terrain_low) * height_scale);
    const math::Vec3 shadow_center{0.0F, 0.5F * (s.terrain_low + s.terrain_high) * height_scale,
                                   0.0F};
    const auto env = environment ? environment->textures
                                 : render::pbr_environment_texture_bindings(*s.environment);
    const auto direction = environment
                               ? glm::normalize(environment->lighting.primary_light_direction)
                               : glm::normalize(math::Vec3{-0.35F, 0.42F, 0.84F});
    const auto sun = environment ? environment->lighting.primary_light_color *
                                       environment->lighting.primary_light_intensity
                                 : math::Vec3{2.6F, 2.4F, 2.1F};
    s.shadow_matrix = math::orthographic(-shadow_domain, shadow_domain, -shadow_domain,
                                         shadow_domain, 1.0F, shadow_domain * 6.0F) *
                      glm::lookAt(shadow_center + direction * shadow_domain * 3.0F, shadow_center,
                                  math::Vec3{0.0F, 1.0F, 0.0F});
    Push push{camera.view_projection,
              {float(config.grid_width), float(config.grid_height), config.cell_size_m,
               options.terrain_height_scale.value_or(1.0F)},
              {camera.position, config.minimum_wet_depth_m},
              {bspline ? float(subdivision) : 0.0F, 0.0F,
               options.terrain_thin_water_composite ? 1.0F : 0.0F, 7.0F},
              {float(terrain_view), material.terrain_material_blend,
               material.terrain_normal_strength,
               float((options.native_bilinear_water ? 8U : 0U) +
                     (bspline ? 16U + (subdivision << 5U) : 0U) +
                     (options.native_display_coverage ? 512U : 0U))}};
    s.frame->upload(
        slot,
        {glm::inverse(camera.view_projection),
         s.shadow_matrix,
         {direction, environment ? environment->exposure : 0.4F},
         {sun, float(env.prefiltered_mip_levels)},
         {float(std::fmod(visual_clock_s, 1024.0)), srgb_attachment(target.format) ? 0.0F : 1.0F,
          float(target.extent.width), float(target.extent.height)},
         {material.wet_roughness, material.wet_darkening, material.terrain_saturation,
          material.terrain_ambient},
         {material.terrain_direct, material.shadow_strength, material.water_roughness,
          material.water_normal_strength},
         {material.water_extinction_scale, material.water_scatter_scale,
          material.water_reflection_scale, material.water_scatter_lighting},
         {material.water_clarity, material.terrain_mineral_scale, 0.0F,
          material.terrain_ambient_softening},
         {material.terrain_diffuse_convolution, material.terrain_specular_scale,
          material.terrain_shadow_scale, material.terrain_slope_color_scale},
         {material.film_begin_m, material.film_end_m, material.film_roughness,
          material.film_ground_mix},
         {float(water_view), material.water_wet_normal, material.water_ripple_strength,
          material.water_ripple_scale_m},
         {float(surface_mode), float(config.grid_width), float(config.grid_height), 0.0F},
         {environment ? 1.0F : 0.0F, material.daylight_sun_scale, 0.0F, 0.0F},
         {material.water_shallow_extinction_boost, material.water_shallow_extinction_end_m, 0.0F,
          0.0F}});
    render::RenderGraphBuilder graph;
    const auto final_state = target_mode == Fluid25DRenderTargetMode::Present
                                 ? render::render_graph_present_texture_state()
                                 : render::render_graph_color_attachment_texture_state();
    const auto back =
        graph.import_color_target("scenic backbuffer", target,
                                  target_mode == Fluid25DRenderTargetMode::Present
                                      ? render::render_graph_undefined_texture_state()
                                      : render::render_graph_color_attachment_texture_state(),
                                  final_state);
    const auto shadow_depth = graph.create_texture(
        texture("scenic sun shadow", kShadowExtent, VK_FORMAT_D32_SFLOAT, true));
    const auto opaque =
        graph.create_texture(texture("scenic opaque HDR", target.extent, kHdrFormat));
    const auto depth = graph.create_texture(
        texture("scenic opaque depth", target.extent, VK_FORMAT_D32_SFLOAT, true));
    const auto composed =
        graph.create_texture(texture("scenic composed HDR", target.extent, kHdrFormat));
    const auto bed = graph.import_buffer(
        {.label = "scenic immutable bed", .byte_size = resources.terrain().size()},
        resources.terrain().handle());
    const auto h =
        graph.import_buffer({.label = "scenic held h", .byte_size = resources.depth_a().size()},
                            resources.depth_a().handle());
    const auto u =
        graph.import_buffer({.label = "scenic held u", .byte_size = resources.velocity().size()},
                            resources.velocity().handle());
    const auto mask = graph.import_buffer(
        {.label = "scenic display coverage", .byte_size = resources.display_coverage().size()},
        resources.display_coverage().handle());
    const auto flow = graph.import_buffer(
        {.label = "scenic render-only filtered velocity", .byte_size = s.visual_flow->size()},
        s.visual_flow->handle());
    const auto set = s.frame->set(slot);
    const auto fields = resources.render_descriptor_set();
    const bool reset_flow =
        reset_visual_flow || !s.flow_initialized || visual_clock_s < s.previous_clock_s;
    const float delta =
        static_cast<float>(std::clamp(visual_clock_s - s.previous_clock_s, 0.0, 0.1));
    const math::Vec4 flow_push{float(fluid_25d_cell_count(config)), 1.0F - std::exp(-delta / 0.25F),
                               reset_flow ? 1.0F : 0.0F, 0.0F};
    s.previous_clock_s = visual_clock_s;
    s.flow_initialized = true;
    graph.add_pass("scenic visual flow filter", render::RenderGraphQueueDomain::Compute)
        .read_storage_buffer(u)
        .read_write_storage_buffer(flow)
        .execute([&, flow_push](const auto& ctx) {
            vulkan::GpuTimestampScope timing(profiler, commands, slot.index,
                                             "fluid_25d scenic flow filter");
            // The display buffer survives frames. Serialize earlier fragment reads
            // against this write on the same queue; no hydraulic write access.
            vulkan::record_memory_barrier(
                commands, {.src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           .src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                           .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT});
            const auto& p = *s.visual_flow_pipeline;
            const auto& r = ctx.recorder();
            r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline());
            r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, p.layout(), 0U,
                                  s.visual_flow_set->set());
            r.push_constants(p.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0U, flow_push);
            r.dispatch(static_cast<std::uint32_t>((fluid_25d_cell_count(config) + 255U) / 256U), 1U,
                       1U);
        });
    graph.add_pass("scenic shadow", render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(bed)
        .read_storage_buffer(h)
        .write_depth(shadow_depth)
        .execute([&, push, fields, set, vertices, shadow_depth, bspline](const auto& ctx) {
            vulkan::GpuTimestampScope timing(profiler, commands, slot.index,
                                             "fluid_25d scenic shadow");
            auto shadow_push = push;
            shadow_push.view_projection = s.shadow_matrix;
            render::record_depth_only_pass(ctx.recorder(),
                                           render::resolved_depth_target_view(ctx, shadow_depth),
                                           render::depth_clear_value(), [&](const auto& r) {
                                               draw(r, bspline ? *s.shadow_bspline : *s.shadow,
                                                    fields, set, shadow_push, vertices);
                                           });
        });
    graph.add_pass("scenic opaque", render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(bed)
        .read_storage_buffer(h)
        .read_storage_buffer(u)
        .read_texture(shadow_depth)
        .write_color(opaque)
        .write_depth(depth)
        .execute([&, push, fields, set, vertices, opaque, depth, bspline](const auto& ctx) {
            vulkan::GpuTimestampScope timing(profiler, commands, slot.index,
                                             "fluid_25d scenic opaque");
            render::record_render_target_pass(
                ctx.recorder(),
                render::render_target_view(render::resolved_color_target_view(ctx, opaque),
                                           render::resolved_depth_target_view(ctx, depth)),
                {.depth = render::depth_clear_value()},
                {.color = vulkan::clear_store_attachment_ops(),
                 .depth = vulkan::clear_store_attachment_ops()},
                [&](const auto& r) {
                    r.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, s.sky->pipeline());
                    r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, s.sky->layout(), 1U,
                                          set);
                    r.draw(3U);
                    const auto& terrain =
                        legacy_terrain ? (bspline ? *s.terrain_bspline_legacy : *s.terrain_legacy)
                                       : (bspline ? *s.terrain_bspline : *s.terrain);
                    draw(r, terrain, fields, set, push, vertices);
                });
        });
    graph.add_pass("scenic water", render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(bed)
        .read_storage_buffer(h)
        .read_storage_buffer(u)
        .read_storage_buffer(mask)
        .read_storage_buffer(flow)
        .read_texture(shadow_depth)
        .read_texture(opaque)
        .read_texture(depth)
        .write_color(composed)
        .execute([&, push, fields, set, vertices, composed, bspline](const auto& ctx) {
            vulkan::GpuTimestampScope timing(profiler, commands, slot.index,
                                             "fluid_25d scenic water");
            render::record_render_target_pass(
                ctx.recorder(),
                render::render_target_view(render::resolved_color_target_view(ctx, composed)), {},
                [&](const auto& r) {
                    r.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, s.copy->pipeline());
                    r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, s.copy->layout(), 1U,
                                          set);
                    r.draw(3U);
                    if (terrain_view == 0U) {
                        const auto& pipeline = bspline ? *s.water_bspline : *s.water;
                        draw(r, pipeline, fields, set, push, vertices);
                    }
                });
        });
    graph.add_pass("scenic display and diagnostic dots", render::RenderGraphQueueDomain::Graphics)
        .read_texture(composed)
        .write_color(back)
        .write_depth(depth)
        .execute([&, set, back, depth, push, markers, marker_fraction](const auto& ctx) {
            vulkan::GpuTimestampScope timing(profiler, commands, slot.index,
                                             "fluid_25d scenic display");
            render::record_render_target_pass(
                ctx.recorder(),
                render::render_target_view(render::resolved_color_target_view(ctx, back),
                                           render::resolved_depth_target_view(ctx, depth)),
                {},
                {.color = vulkan::clear_store_attachment_ops(),
                 .depth = vulkan::load_store_attachment_ops()},
                [&](const auto& r) {
                    r.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, s.display->pipeline());
                    r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, s.display->layout(), 1U,
                                          set);
                    r.draw(3U);
                    if (markers && terrain_view == 0U && water_view == 0U)
                        markers->record_draw(
                            r.handle(), resources.current_depth_is_a(), push.view_projection,
                            target.extent, push.grid_cell.w, marker_fraction,
                            bspline || options.native_bilinear_water, bspline ? subdivision : 0U);
                });
        });
    const auto compiled = graph.compile();
    executor.record(
        {.device = &device,
         .command_buffer = commands,
         .frame_slot = slot,
         .label = "native Scenic presentation",
         .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording},
        compiled, [&](const render::RenderGraphResourceSet& images) {
            render::MaterialDescriptorWriter writer(set);
            writer.combined_image_sampler(1U, env.prefiltered_sampler, env.prefiltered_view)
                .combined_image_sampler(2U, env.irradiance_sampler, env.irradiance_view)
                .combined_image_sampler(3U, env.brdf_lut_sampler, env.brdf_lut_view)
                .combined_image_sampler(4U, s.detail->sampler().handle(), s.detail->view());
            writer.storage_buffer(9U, s.visual_flow->handle(), s.visual_flow->size());
            const auto& daylight = s.captured_daylight ? *s.captured_daylight : *s.visual_flow;
            writer.storage_buffer(14U, daylight.handle(), daylight.size());
            writer.combined_image_sampler(
                10U,
                s.terrain_diffuse ? s.terrain_diffuse->sampler().handle() : env.irradiance_sampler,
                s.terrain_diffuse ? s.terrain_diffuse->view() : env.irradiance_view);
            for (const auto& [binding, surface] :
                 std::array<std::pair<unsigned, const render::Texture2D*>, 3>{
                     {{11U, s.landform_surface ? &*s.landform_surface : &*s.detail},
                      {12U, s.climate_surface ? &*s.climate_surface : &*s.detail},
                      {13U, s.correlated_surface ? &*s.correlated_surface : &*s.detail}}})
                writer.combined_image_sampler(binding, surface->sampler().handle(),
                                              surface->view());
            for (const auto& [binding, handle] :
                 std::array<std::pair<std::uint32_t, render::RenderGraphTextureHandle>, 4>{
                     {{5U, shadow_depth}, {6U, opaque}, {7U, depth}, {8U, composed}}}) {
                const auto image = render::resolved_sampled_texture_view(compiled, images, handle);
                writer.combined_image_sampler(binding,
                                              (binding == 5U || binding == 7U)
                                                  ? s.nearest_sampler->handle()
                                                  : s.linear_sampler->handle(),
                                              image.view, image.layout);
            }
            writer.update(device);
        });
}
} // namespace cubey::projects::fluid::fluid_25d
