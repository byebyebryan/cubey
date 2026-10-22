#include "sdf_renderer.h"

#include <cubey/render/pass.h>
#include <cubey/render/render_graph_builder.h>
#include <cubey/render/render_graph_resolve.h>
#include <cubey/vulkan/command_recorder.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace cubey::projects::sdf {
namespace {
struct Camera {
    math::Mat4 inverse_vp;
    math::Vec4 eye, sunlight, options, forward;
};
static_assert(sizeof(Camera) == 128);
constexpr auto hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
} // namespace
void Renderer::create(vulkan::Device& device, VkExtent2D extent, VkFormat format,
                      std::uint32_t slots, const Field& field, const SdfConfig& config,
                      const AuthoredScene* scene) {
    field_ = &field;
    scene_extent_ = {extent.width * config.supersample, extent.height * config.supersample};
    executor_.resize(slots);
    lighting_.emplace(device, slots);
    const auto host_buffer = [&](std::size_t bytes) {
        return std::make_unique<vulkan::Buffer>(
            device,
            vulkan::BufferConfig{.size = bytes,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
    };
    scene_program_ = host_buffer(16 + authored_max_instructions * sizeof(SceneInstruction));
    scene_materials_ = host_buffer(16 * sizeof(SurfaceMaterial));
    update_authored(scene);
    const std::array<vulkan::DescriptorSetBindingConfig, 5> bindings{
        {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
         {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
         {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
         {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
         {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT}}};
    descriptors_ = std::make_unique<vulkan::DescriptorSetArray>(
        device, vulkan::DescriptorSetInfo(bindings, slots));
    update_field(device, field);
    for (std::uint32_t i = 0; i < slots; ++i) {
        vulkan::DescriptorWriteBatch writes;
        writes.uniform_buffer(descriptors_->set(i), 2, lighting_->buffer({i, slots}).handle(),
                              sizeof(LightingUniforms));
        writes.storage_buffer(descriptors_->set(i), 3, scene_program_->handle(),
                              scene_program_->size());
        writes.storage_buffer(descriptors_->set(i), 4, scene_materials_->handle(),
                              scene_materials_->size());
        writes.update(device);
    }
    const auto path = std::filesystem::path(CUBEY_SDF_SHADER_DIR);
    const std::array stages{
        render::vertex_shader_file(path / "sdf.vert.spv"),
        render::fragment_shader_file(path / (scene ? "sdf_authored.frag.spv" : "sdf.frag.spv"))};
    const std::array layouts{descriptors_->layout()};
    render::MaterialPassInfo pass{
        .label = "sdf.trace",
        .push_constants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Camera)}},
        .depth_test = true,
        .depth_write = true,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL};
    pipeline_.emplace(
        device, render::GraphicsPipelineFileResourceConfig{.extent = scene_extent_,
                                                           .color_format = hdr_format,
                                                           .depth_format = VK_FORMAT_D32_SFLOAT,
                                                           .shader_stage_files = stages,
                                                           .descriptor_set_layouts = layouts,
                                                           .material_pass = pass});
    const std::array raster_stages{render::vertex_shader_file(path / "occluder.vert.spv"),
                                   render::fragment_shader_file(path / "occluder.frag.spv")};
    pass.push_constants = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera)}};
    occluder_pipeline_.emplace(
        device, render::GraphicsPipelineFileResourceConfig{.extent = scene_extent_,
                                                           .color_format = hdr_format,
                                                           .depth_format = VK_FORMAT_D32_SFLOAT,
                                                           .shader_stage_files = raster_stages,
                                                           .material_pass = pass});
    post_.create_materials(device, {.frame_slot_count = slots});
    const std::array post_stages{render::vertex_shader_file(path / "forward_pbr_post.vert.spv"),
                                 render::fragment_shader_file(path / "forward_pbr_post.frag.spv")};
    post_.create_pipeline(
        device, {.extent = extent, .color_format = format, .shader_stage_files = post_stages});
    if (!config.common.profile_output_prefix.empty() || config.common.print_frame_stats)
        profiler_.emplace(device, slots, 4);
    recorded_frames_.resize(slots);
    render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = config.sun_elevation;
    environment.sun_azimuth_degrees = config.sun_azimuth;
    environment.moon.enabled = false;
    environment.reference_geometry_enabled = false;
    atmosphere_.set_environment(environment);
}
void Renderer::update_field(vulkan::Device& device, const Field& field) {
    field_ = &field;
    for (std::uint32_t i = 0; i < descriptors_->size(); ++i) {
        vulkan::DescriptorWriteBatch writes;
        writes.storage_buffer(descriptors_->set(i), 0, field.nodes().handle(),
                              field.nodes().size());
        writes.combined_image_sampler(descriptors_->set(i), 1, field.atlas().sampler().handle(),
                                      field.atlas().view(),
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        writes.update(device);
    }
}
void Renderer::destroy() {
    executor_.clear();
    profiler_.reset();
    recorded_frames_.clear();
    post_.destroy();
    occluder_pipeline_.reset();
    pipeline_.reset();
    descriptors_.reset();
    scene_materials_.reset();
    scene_program_.reset();
    lighting_.reset();
    atmosphere_.destroy();
    field_ = nullptr;
}
void Renderer::update_authored(const AuthoredScene* scene) {
    const auto bytes = authored_program_bytes(scene);
    scene_program_->upload(bytes.data(), bytes.size());
    const SurfaceMaterial fallback;
    scene_materials_->upload(scene ? scene->materials.data() : &fallback,
                             scene ? scene->materials.size() * sizeof(SurfaceMaterial)
                                   : sizeof(SurfaceMaterial));
}
void Renderer::record(vulkan::Device& device, VkCommandBuffer command,
                      render::ColorTargetView target, render::FrameSlot slot, bool present,
                      const SdfConfig& config, float yaw, float pitch, float distance,
                      profiling::ProfileRecorder* profile, std::uint64_t frame_index,
                      const BoxEdit* cutter, float cutter_opacity,
                      const SceneInstruction* preview) {
    auto environment = atmosphere_.environment();
    environment.sun_elevation_degrees = config.sun_elevation;
    environment.sun_azimuth_degrees = config.sun_azimuth;
    atmosphere_.set_environment(environment);
    if (profiler_) {
        profiler_->collect(slot.index);
        if (recorded_frames_[slot.index] && profile)
            for (const auto& timing : profiler_->latest_timings())
                profile->record_gpu_span(*recorded_frames_[slot.index], timing.label,
                                         timing.milliseconds);
        profiler_->begin_frame(command, slot.index);
        recorded_frames_[slot.index] = frame_index;
    }
    const math::Vec3 target_point{0, config.target_y, 0};
    const math::Vec3 eye =
        target_point + math::Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                  std::cos(pitch) * std::cos(yaw)} *
                           distance;
    const auto vp = math::perspective(glm::radians(45.0F),
                                      static_cast<float>(target.extent.width) /
                                          static_cast<float>(target.extent.height),
                                      0.01F, 100.0F) *
                    glm::lookAt(eye, target_point, math::Vec3{0, 1, 0});
    const auto& light = atmosphere_.lighting();
    LightingUniforms uniforms{};
    uniforms.atmosphere = atmosphere_.frame({}).background;
    uniforms.direct_color = {light.primary_light_color * light.primary_light_intensity, 0};
    for (unsigned i = 0; i < 9; ++i)
        uniforms.irradiance[i] = {light.diffuse_irradiance_sh[i], 0};
    uniforms.material =
        config.clay ? math::Vec4{0.42F, 0.42F, 0.42F, 1} : math::Vec4{0.55F, 0.27F, 0.095F, 0};
    uniforms.quality = {config.roughness, config.shadow_softness, config.ao ? 1.0F : 0.0F, -0.48F};
    if (cutter && config.cutter_cue && config.debug == 0) {
        uniforms.cutter = gpu_edit(*cutter);
        uniforms.cutter_style = {cutter_opacity, 0.0025F, 0, 0};
    }
    if (preview && config.cutter_cue && config.debug == 0) {
        uniforms.preview = *preview;
        uniforms.preview_style.x = cutter_opacity;
    }
    lighting_->upload(slot, uniforms);
    const Camera camera{glm::inverse(vp),
                        {eye, static_cast<float>(field_->atlas_axis)},
                        {light.primary_light_direction, 0},
                        {static_cast<float>(config.debug), static_cast<float>(config.shadows),
                         config.ground ? 1.0F : 0.0F, 0},
                        {glm::normalize(target_point - eye), 0}};
    post_.upload(slot, render::hdr_post_uniforms(
                           target.format, config.debug ? 0.0F : config.exposure,
                           config.debug ? render::PbrTonemap::Linear : render::PbrTonemap::Aces));
    render::RenderGraphBuilder builder;
    const auto output = builder.import_color_target(
        "sdf.output", target,
        present ? render::render_graph_undefined_texture_state()
                : render::render_graph_color_attachment_texture_state(),
        present ? render::render_graph_present_texture_state()
                : render::render_graph_color_attachment_texture_state());
    const auto scene = builder.create_texture(
        render::hdr_scene_color_texture_desc("sdf.hdr", scene_extent_, hdr_format));
    const auto depth =
        builder.create_texture({.label = "sdf.depth",
                                .extent = {scene_extent_.width, scene_extent_.height, 1},
                                .format = VK_FORMAT_D32_SFLOAT,
                                .aspects = VK_IMAGE_ASPECT_DEPTH_BIT});
    const auto atlas = builder.import_texture(
        {.label = "sdf.atlas",
         .extent = field_->atlas().extent(),
         .format = VK_FORMAT_R16_SFLOAT,
         .aspects = VK_IMAGE_ASPECT_COLOR_BIT},
        field_->atlas().handle(), field_->atlas().view(),
        render::RenderGraphTextureState{
            VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
        render::RenderGraphTextureState{VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
                                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT});
    const auto nodes = builder.import_buffer(
        {"sdf.nodes", field_->nodes().size()}, field_->nodes().handle(),
        render::RenderGraphBufferState{VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT});
    const auto lighting = builder.import_buffer({"sdf.lighting", sizeof(LightingUniforms)},
                                                lighting_->buffer(slot).handle());
    const auto program =
        builder.import_buffer({"sdf.scene", scene_program_->size()}, scene_program_->handle());
    const auto materials = builder.import_buffer({"sdf.materials", scene_materials_->size()},
                                                 scene_materials_->handle());
    builder.add_pass("sdf.trace", render::RenderGraphQueueDomain::Graphics)
        .read_texture(atlas, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(nodes, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_uniform_buffer(lighting, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(program, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(materials, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .write_color(scene)
        .write_depth(depth)
        .execute([&, camera, scene, depth](const render::RenderGraphExecutionContext& ctx) {
            render::record_render_target_pass(
                ctx.recorder(),
                render::render_target_view(render::resolved_color_target_view(ctx, scene),
                                           render::resolved_depth_target_view(ctx, depth)),
                {.color = render::color_clear_value(0, 0, 0, 1),
                 .depth = VkClearValue{.depthStencil = {1, 0}}},
                [&](const vulkan::CommandRecorder& pass) {
                    pass.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->pipeline());
                    pass.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                             0, descriptors_->set(slot.index));
                    pass.push_constants(pipeline_->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                        camera);
                    render::record_fullscreen_triangle(pass);
                    if (config.occluder) {
                        pass.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           occluder_pipeline_->pipeline());
                        pass.push_constants(occluder_pipeline_->layout(),
                                            VK_SHADER_STAGE_VERTEX_BIT, 0, camera);
                        pass.draw(6);
                    }
                });
        });
    builder.add_pass("sdf.post", render::RenderGraphQueueDomain::Graphics)
        .read_texture(scene)
        .write_color(output)
        .execute([&, output](const render::RenderGraphExecutionContext& ctx) {
            post_.record_pass(ctx.recorder(), render::resolved_color_target_view(ctx, output),
                              slot);
        });
    const auto graph = builder.compile();
    executor_.record({.device = &device,
                      .command_buffer = command,
                      .frame_slot = slot,
                      .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording,
                      .profiler = profiler_ ? &*profiler_ : nullptr},
                     graph, [&](const render::RenderGraphResourceSet& resources) {
                         post_.update_scene_color_descriptor(device, slot, graph, resources, scene);
                     });
}
} // namespace cubey::projects::sdf
