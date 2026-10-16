#include "terrain_surface_patch_scene_internal.h"

#include <cubey/render/generated_texture.h>
#include <cubey/render/pass.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace cubey {
using render::TerrainSurfacePatchFields;
using render::TerrainSurfacePatchGrid;
using render::TerrainSurfacePatchSurface;
using Uniforms = render::TerrainSurfacePatchUniforms;
using namespace terrain_surface_detail;
namespace {
render::Texture2D upload_surface(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                 std::uint32_t width, std::uint32_t height,
                                 std::span<const math::Vec4> source_values) {
    std::vector<math::Vec4> values(source_values.begin(), source_values.end());
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
} // namespace
TerrainSurfacePatchScene::TerrainSurfacePatchScene(std::filesystem::path directory)
    : shader_directory_(std::move(directory)) {}
std::filesystem::path TerrainSurfacePatchScene::shader(const char* name) const {
    return shader_directory_ / name;
}
TerrainSurfacePatchScene::~TerrainSurfacePatchScene() = default;

bool TerrainSurfacePatchScene::ensure_resources(
    const vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots,
    render::ColorTargetView target, const TerrainSurfacePatchGrid& config,
    std::span<const float> bed_values, const TerrainSurfacePatchFields& fields,
    bool integrated_terrain_diffuse, const TerrainSurfacePatchSurface* surface,
    bool captured_daylight) {
    const auto cells = std::size_t(config.grid_width) * config.grid_height;
    if (slots == 0U || config.grid_width < 2U || config.grid_height < 2U ||
        !std::isfinite(config.cell_size_m) || config.cell_size_m <= 0.0F ||
        bed_values.size() != cells || target.extent.width == 0U || target.extent.height == 0U ||
        target.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("invalid terrain surface scene resources");
    }
    if (surface) {
        const auto samples = std::size_t(surface->width) * surface->height;
        for (const auto values : {surface->landform, surface->climate, surface->correlated}) {
            if (!values.empty() && (samples == 0U || values.size() != samples)) {
                throw std::runtime_error("terrain surface texture dimensions do not match samples");
            }
        }
    }
    if (state_ && state_->frame_slot_count != slots) {
        throw std::runtime_error("terrain surface frame slot changes require full scene teardown");
    }
    bool diffuse_created = false;
    if (!state_) {
        auto candidate = std::make_unique<State>();
        auto& s = *candidate;
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
        s.frame.emplace(device, render::FrameUniformMaterialInstanceConfig{
                                    .material_pass = render::terrain_surface_patch_frame_material(),
                                    .descriptor_set = 1U,
                                    .frame_slot_count = slots});
        s.profiler.emplace(device, slots, 9U);
        s.visual_flow.emplace(
            device, vulkan::device_local_buffer_config(fields.velocity.size(),
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
        flow_writer.storage_buffer(0U, fields.velocity.handle(), fields.velocity.size())
            .storage_buffer(1U, s.visual_flow->handle(), s.visual_flow->size())
            .update(device);
        const std::array<VkPushConstantRange, 1> flow_push{
            {{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0U, .size = 16U}}};
        render::emplace_single_set_compute_pipeline_resource(
            s.visual_flow_pipeline, device,
            render::compute_shader_file(shader("fluid_25d_scenic_flow.comp.spv")),
            s.visual_flow_set->layout(), flow_push);
        const auto [lo, hi] = std::minmax_element(bed_values.begin(), bed_values.end());
        s.terrain_low = *lo;
        s.terrain_high = *hi;
        s.domain_m = static_cast<float>(std::max(config.grid_width - 1U, config.grid_height - 1U)) *
                     config.cell_size_m;
        s.frame_slot_count = slots;
        state_ = std::move(candidate);
    }
    auto& s = *state_;
    if (captured_daylight && !s.captured_daylight) {
        DaylightResources daylight;
        daylight.captured_daylight.emplace(
            device, vulkan::device_local_buffer_config(sizeof(math::Vec4) * 10U,
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        const std::array<vulkan::DescriptorSetBindingConfig, 2> bindings{
            {{.binding = 0U,
              .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
             {.binding = 1U,
              .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT}}};
        for (std::uint32_t i = 0; i < slots; ++i)
            daylight.daylight_sets.push_back(std::make_unique<vulkan::DescriptorSetBundle>(
                device, vulkan::DescriptorSetInfo(bindings)));
        const std::array<VkPushConstantRange, 1> push{{{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                        .offset = 0U,
                                                        .size = sizeof(DaylightPush)}}};
        const std::array layouts{daylight.daylight_sets.front()->layout()};
        daylight.daylight_pipeline = std::make_unique<render::ComputePipelineResource>(
            device,
            render::ComputePipelineResourceConfig{
                .shader_stage = render::compute_shader_file(shader("fluid_25d_daylight.comp.spv")),
                .descriptor_set_layouts = layouts,
                .push_constants = push});
        static_cast<DaylightResources&>(s) = std::move(daylight);
        diffuse_created = true;
    }
    if (surface && !s.landform_surface && !s.correlated_surface) {
        SurfaceResources textures;
        if (!surface->landform.empty())
            textures.landform_surface.emplace(
                upload_surface(device, gpu, surface->width, surface->height, surface->landform));
        if (!surface->climate.empty())
            textures.climate_surface.emplace(
                upload_surface(device, gpu, surface->width, surface->height, surface->climate));
        if (!surface->correlated.empty())
            textures.correlated_surface.emplace(
                upload_surface(device, gpu, surface->width, surface->height, surface->correlated));
        static_cast<SurfaceResources&>(s) = std::move(textures);
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
    if (s.patch && s.extent.width == target.extent.width &&
        s.extent.height == target.extent.height && s.format == target.format)
        return diffuse_created;
    TargetResources candidate;
    candidate.extent = target.extent;
    candidate.format = target.format;
    std::printf("fluid_25d_scenic_resources: extent=%ux%u HDR=rgba16f shadow=runtime-selected\n",
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
    const auto mesh = [&](std::unique_ptr<render::GraphicsPipelineResource>& p, const char* vertex,
                          const char* fragment, bool water) {
        const std::array<render::ShaderStageFile, 2> stages{
            render::vertex_shader_file(shader(vertex)),
            render::fragment_shader_file(shader(fragment))};
        p = std::make_unique<render::GraphicsPipelineResource>(
            device, render::GraphicsPipelineFileResourceConfig{
                        .extent = target.extent,
                        .color_format = kHdrFormat,
                        .depth_format = water ? VK_FORMAT_UNDEFINED : VK_FORMAT_D32_SFLOAT,
                        .shader_stage_files = stages,
                        .descriptor_set_layouts = layouts,
                        .material_pass = render::terrain_surface_patch_mesh_pass(water)});
    };
    candidate.patch = std::make_unique<render::TerrainSurfacePatchRenderer>(
        device, target.extent, field_layout.layout(), s.frame->layout(), shader_directory_);
    mesh(candidate.rain, "fluid_25d_rain_visual.vert.spv", "fluid_25d_rain_visual.frag.spv", true);
    const auto fullscreen = [&](std::unique_ptr<render::GraphicsPipelineResource>& p,
                                const char* fragment, VkFormat format, bool depth) {
        const std::array<render::ShaderStageFile, 2> stages{
            render::vertex_shader_file(shader("fluid_25d.vert.spv")),
            render::fragment_shader_file(shader(fragment))};
        p = std::make_unique<render::GraphicsPipelineResource>(
            device, render::GraphicsPipelineFileResourceConfig{
                        .extent = target.extent,
                        .color_format = format,
                        .depth_format = depth ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED,
                        .shader_stage_files = stages,
                        .descriptor_set_layouts = layouts,
                        .material_pass = {.label = "fluid_25d.scenic.fullscreen"}});
    };
    fullscreen(candidate.sky, "fluid_25d_scenic_sky.frag.spv", kHdrFormat, true);
    fullscreen(candidate.copy, "fluid_25d_scenic_copy.frag.spv", kHdrFormat, false);
    fullscreen(candidate.display, "fluid_25d_scenic_display.frag.spv", target.format, true);
    static_cast<TargetResources&>(s) = std::move(candidate);
    return true;
}

void TerrainSurfacePatchScene::destroy_swapchain_resources() {
    if (!state_)
        return;
    static_cast<TargetResources&>(*state_) = {};
}
void TerrainSurfacePatchScene::destroy() {
    state_.reset();
}

} // namespace cubey
