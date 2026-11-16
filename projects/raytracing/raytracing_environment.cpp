#include "raytracing_environment.h"
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/gpu_timestamps.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/vk_check.h>
#include <glm/gtc/constants.hpp>
#include <set>

namespace cubey::projects::raytracing {
namespace {
auto shader(const char* p) {
    return std::filesystem::path(CUBEY_RAYTRACING_SHADER_DIR) / p;
}
render::AtmosphereEnvironmentConfig state(const Config& c, float eye_y) {
    render::AtmosphereEnvironmentConfig e;
    e.sun_elevation_degrees = glm::degrees(c.sun_elevation);
    e.sun_azimuth_degrees = shared_sun_azimuth_degrees(c.sun_azimuth);
    e.camera_altitude_km = c.scene == "materials" ? .002F : std::max(eye_y, .05F) * .001F;
    e.moon.enabled = false;
    e.render_night_sky = false;
    e.render_moon_disk = false;
    e.reference_geometry_enabled = false;
    e.ground_mode = render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
    return e;
}
} // namespace
Environment::Environment(vulkan::Device& device, vulkan::GpuRuntime& gpu, const Config& c,
                         std::uint32_t slots) {
    const auto start = std::chrono::steady_clock::now();
    active_ = c.environment == "atmosphere";
    sampled_ = sampled_material_scene(c);
    if (c.scene == "materials")
        probe_ground_mode_ = render::AtmosphereEnvironmentGroundMode::Ground;
    slots_ = slots;
    try {
        fallback_.emplace(
            render::create_generated_pbr_environment(device, gpu,
                                                     {.irradiance_extent = active_ ? 4U : 1U,
                                                      .prefiltered_extent = active_ ? 4U : 1U,
                                                      .prefiltered_mip_levels = 1,
                                                      .brdf_lut_extent = active_ ? 128U : 1U}));
        fallback_ms_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (active_) {
            atmosphere_.set_environment(state(c, c.target_y + std::sin(c.pitch) * c.distance));
            placeholders_.emplace(
                render::create_atmosphere_background_placeholder_textures(device, gpu));
            atmosphere_.create_resources(
                device, {.reflection_extent = 128,
                         .reflection_mip_levels = 5,
                         .reflection_update_hz = c.lighting_cycle == "frozen" ? 4.F : 30.F,
                         .frame_slot_count = slots,
                         .atmosphere_textures = placeholders_->bindings(),
                         .reflection_ground_mode = probe_ground_mode_,
                         .consumer_stage_mask = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR});
            if (sampled_)
                background_.create_materials(
                    device, {.frame_slot_count = slots, .textures = placeholders_->bindings()});
            const auto pipeline_start = std::chrono::steady_clock::now();
            atmosphere_.create_pipelines(
                device, {.atmosphere_vertex_shader = shader("atmosphere.vert.spv"),
                         .atmosphere_fragment_shader = shader("atmosphere.frag.spv"),
                         .reflection_prefilter_vertex_shader = shader("atmosphere.vert.spv"),
                         .reflection_prefilter_fragment_shader =
                             shader("atmosphere_reflection_prefilter.frag.spv")});
            if (sampled_) {
                sun_component_ = std::make_unique<render::TextureCube>(
                    device, render::TextureCubeConfig{
                                .extent = 128,
                                .mip_levels = 1,
                                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                .usage = render::TextureCubeUsage::ColorAttachmentSampled,
                                .create_sampler = true,
                                .sampler = {}});
                for (std::uint32_t face = 0; face < 6; ++face)
                    sun_faces_.push_back(
                        render::create_texture_cube_face_view(device, *sun_component_, 0, face));
                sun_background_.create_materials(
                    device, {.frame_slot_count = slots * 6, .textures = placeholders_->bindings()});
                const std::array stages{
                    render::vertex_shader_file(shader("atmosphere.vert.spv")),
                    render::fragment_shader_file(shader("atmosphere.frag.spv"))};
                sun_background_.create_pipeline(device,
                                                {.extent = {128, 128},
                                                 .color_format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                                 .shader_stage_files = stages});
            }
            pipeline_ms_ = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - pipeline_start)
                               .count();
            (void)gpu.submit_and_wait(
                {.label = "raytracing settle shared sky",
                 .work = [&](vulkan::GpuOwnerContext& owner) {
                     vulkan::ImmediateCommands commands(owner);
                     vulkan::GpuTimestampProfiler timer(device, 1, 1);
                     timer.begin_frame(commands.command_buffer(), 0);
                     timer.begin_pass(commands.command_buffer(), 0, "environment.probe-build");
                     atmosphere_.record_pending_update(
                         vulkan::CommandRecorder(commands.command_buffer()), {0, slots});
                     timer.end_pass(commands.command_buffer(), 0);
                     commands.submit_and_wait();
                     timer.collect(0);
                     for (const auto& value : timer.latest_timings())
                         probe_gpu_ms_ += value.milliseconds;
                 }});
            atmosphere_.advance(.25);
            if (!atmosphere_.reflection_probe().snapshot().valid)
                throw std::runtime_error("RT sky did not settle");
        }
    } catch (...) {
        sun_background_.destroy();
        background_.destroy();
        atmosphere_.destroy();
        throw;
    }
    setup_ms_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
Environment::~Environment() {
    sun_background_.destroy();
    background_.destroy();
    atmosphere_.destroy();
}
EnvironmentUniform Environment::prepare(const Config& c, VkCommandBuffer command,
                                        render::FrameSlot slot, math::Vec3 eye, math::Vec3& sun) {
    EnvironmentUniform result;
    result.transport = {
        c.reflection_source == "scene" ? 1.F : 0.F, c.transmission_source == "scene" ? 1.F : 0.F,
        c.scene.find("refraction-") == 0 || c.scene == "slab" ? 1.F : 0.F,
        c.scene.find("reflection-") == 0 || c.scene.find("refraction-") == 0 || c.scene == "slab"
            ? 1.F
            : 0.F};
    result.limits = {c.max_events, c.max_path_rays, c.max_total_rays,
                     c.scene == "dragon-optics" ? c.max_events : 8};
    result.glass = {c.ior, c.attenuation_distance, c.throughput_cutoff,
                    c.scene == "dragon-optics" ? (c.hide_dragon ? 2.F : 1.F) : 0.F};
    // The spare fourth component selects the optical dragon's interface normal.
    // It is part of the environment/history identity; other scenes keep zero.
    result.attenuation = {c.attenuation_red, c.attenuation_green, c.attenuation_blue,
                          c.scene == "dragon-optics" && c.smooth_normals ? 1.F : 0.F};
    if (active_) {
        atmosphere_.set_environment(state(c, eye.y));
        const auto now = std::chrono::steady_clock::now();
        double delta = c.common.headless ? (c.sun_speed != 0 ? 1.0 / std::max(c.common.fps, 1U) : 0)
                                         : std::chrono::duration<double>(now - previous_).count();
        if (c.presentation == "library" && c.lighting_cycle != "frozen") {
            delta = previous_presentation_seconds_
                        ? std::max(0.0, c.presentation_time - *previous_presentation_seconds_)
                        : 0;
            previous_presentation_seconds_ = c.presentation_time;
        }
        atmosphere_.advance(delta);
        previous_ = now;
        atmosphere_.record_pending_update(vulkan::CommandRecorder(command), slot);
        const auto& light = atmosphere_.lighting();
        sun = light.primary_light_direction;
        result.direct_color = {
            light.primary_light_color * light.primary_light_intensity * (c.intensity / 3.F), 0};
        for (std::size_t i = 0; i < 9; ++i)
            result.irradiance[i] = {light.diffuse_irradiance_sh[i] * (c.intensity / 3.F), 0};
        const auto bindings = atmosphere_.pbr_environment_bindings(*fallback_, false);
        result.options = {bindings.prefiltered_blend, float(bindings.prefiltered_mip_levels - 1), 1,
                          c.intensity / 3.F};
    }
    return result;
}
void Environment::resize_background(const vulkan::Device& device, VkExtent2D extent) {
    if (!sampled_)
        return;
    std::vector<BackgroundTarget> next;
    for (std::uint32_t i = 0; i < slots_; ++i)
        next.push_back({std::make_unique<render::Texture2D>(
            device, render::Texture2DConfig{.extent = extent,
                                            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                            .usage = render::Texture2DUsage::ColorAttachmentSampled,
                                            .create_sampler = true,
                                            .sampler = {}})});
    const std::array stages{render::vertex_shader_file(shader("atmosphere.vert.spv")),
                            render::fragment_shader_file(shader("atmosphere.frag.spv"))};
    background_.create_pipeline(device, {.extent = extent,
                                         .color_format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                         .shader_stage_files = stages});
    background_targets_ = std::move(next);
}
void Environment::record_background(VkCommandBuffer command, render::FrameSlot slot,
                                    const math::Mat4& inverse_view, float aspect,
                                    float tan_half_fovy) {
    if (!sampled_)
        return;
    if (sun_generation_ != generation()) {
        auto barrier =
            vulkan::vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        const bool initialized = sun_generation_ != UINT64_MAX;
        barrier.oldLayout =
            initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = sun_component_->handle();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
        vkCmdPipelineBarrier(command,
                             initialized ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
                                         : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
        auto environment = atmosphere_.environment();
        environment.ground_mode = probe_ground_mode_;
        const auto faces = render::atmosphere_reflection_probe_cube_face_view_rays();
        for (std::uint32_t face = 0; face < 6; ++face) {
            const render::FrameSlot face_slot{slot.index * 6 + face, slots_ * 6};
            sun_background_.upload(
                face_slot, render::atmosphere_environment_frame_uniforms(
                               environment,
                               {.view_rays = faces[face],
                                .render_view = render::AtmosphereEnvironmentRenderView::SunDisk}));
            sun_background_.record_pass(vulkan::CommandRecorder(command),
                                        {{128, 128},
                                         VK_FORMAT_R16G16B16A16_SFLOAT,
                                         sun_component_->handle(),
                                         sun_faces_[face].handle()},
                                        face_slot);
        }
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
        sun_generation_ = generation();
    }
    auto& target = background_targets_.at(slot.index);
    auto& texture = *target.color;
    render::ViewRayBasis3D rays{{math::Vec3(inverse_view[0]), aspect},
                                {math::Vec3(inverse_view[1]), tan_half_fovy},
                                {-math::Vec3(inverse_view[2]), 0}};
    background_.upload(slot, atmosphere_.frame({.view_rays = rays}).background);
    auto barrier = vulkan::vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.oldLayout =
        target.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = target.initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.handle();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command,
                         target.initialized ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
                                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
    background_.record_pass(vulkan::CommandRecorder(command),
                            {texture.extent(), texture.format(), texture.handle(), texture.view()},
                            slot);
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    target.initialized = true;
}
void Environment::bind(const vulkan::Device& device, VkDescriptorSet set,
                       std::uint32_t slot) const {
    const auto bindings = active_ ? atmosphere_.pbr_environment_bindings(*fallback_, false)
                                  : render::pbr_environment_texture_bindings(*fallback_);
    const auto& sky =
        active_ ? atmosphere_.reflection_probe().sky_radiance_cube() : fallback_->prefiltered_cube;
    vulkan::DescriptorWriteBatch{}
        .combined_image_sampler(set, 7, sky.sampler().handle(), sky.view(),
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, 8, bindings.prefiltered_sampler, bindings.prefiltered_view,
                                bindings.prefiltered_layout)
        .combined_image_sampler(set, 9, bindings.previous_prefiltered_sampler,
                                bindings.previous_prefiltered_view,
                                bindings.previous_prefiltered_layout)
        .combined_image_sampler(set, 10, bindings.brdf_lut_sampler, bindings.brdf_lut_view,
                                bindings.brdf_lut_layout)
        .combined_image_sampler(
            set, 16,
            background_targets_.empty() ? bindings.brdf_lut_sampler
                                        : background_targets_.at(slot).color->sampler().handle(),
            background_targets_.empty() ? bindings.brdf_lut_view
                                        : background_targets_.at(slot).color->view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .combined_image_sampler(set, 17,
                                sun_component_ ? sun_component_->sampler().handle()
                                               : bindings.prefiltered_sampler,
                                sun_component_ ? sun_component_->view() : bindings.prefiltered_view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .update(device);
}
std::uint64_t Environment::generation() const {
    return active_ ? atmosphere_.reflection_probe().snapshot().generation : 0;
}
std::uint64_t Environment::texture_allocation_bytes(const vulkan::Device& device) const {
    std::set<VkImage> images{fallback_->irradiance_cube.handle(),
                             fallback_->prefiltered_cube.handle(), fallback_->brdf_lut.handle()};
    if (sun_component_)
        images.insert(sun_component_->handle());
    if (active_) {
        const auto snapshot = atmosphere_.reflection_probe().snapshot();
        images.insert(atmosphere_.reflection_probe().sky_radiance_cube().handle());
        if (snapshot.current)
            images.insert(snapshot.current->handle());
        if (snapshot.previous)
            images.insert(snapshot.previous->handle());
        images.insert(placeholders_->lunar_surface.handle());
        images.insert(placeholders_->night_sky.handle());
    }
    std::uint64_t bytes = 0;
    if (active_) {
        const auto snapshot = atmosphere_.reflection_probe().snapshot();
        if (snapshot.current == snapshot.previous) {
            VkMemoryRequirements spare{};
            vkGetImageMemoryRequirements(device.handle(), snapshot.current->handle(), &spare);
            bytes += spare.size;
        }
    }
    for (auto image : images) {
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device.handle(), image, &requirements);
        bytes += requirements.size;
    }
    for (const auto& target : background_targets_) {
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device.handle(), target.color->handle(), &requirements);
        bytes += requirements.size;
    }
    return bytes;
}
} // namespace cubey::projects::raytracing
