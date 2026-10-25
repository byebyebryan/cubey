#include "sdf_renderer.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph_builder.h>
#include <cubey/render/render_graph_resolve.h>
#include <cubey/vulkan/command_recorder.h>

#include <chrono>
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
void Renderer::create(vulkan::Device& device, vulkan::GpuRuntime& gpu, VkExtent2D extent,
                      VkFormat format, std::uint32_t slots, const Field& field,
                      const SdfConfig& config, const AuthoredScene* scene) {
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
    scene_patterns_ = host_buffer(16 * sizeof(MaterialPattern));
    scene_domains_ = host_buffer(16 * sizeof(math::Mat4));
    scene_libraries_ = host_buffer(16 * sizeof(procedural::SurfaceLibrary));
    const bool study = config.lighting != "atmosphere";
    if (study) {
        const auto start = std::chrono::steady_clock::now();
        if (!config.material_environment_path.empty()) {
            if (config.lighting != "material-study")
                throw std::invalid_argument("diagnostic HDR requires material-study lighting");
            const auto image = asset::load_hdr_image(config.material_environment_path);
            study_environment_.emplace(render::create_pbr_environment_from_equirectangular(
                device, gpu,
                {.width = image.width, .height = image.height, .rgba32f = image.rgba32f}));
        } else
            study_environment_.emplace(render::create_generated_pbr_environment(device, gpu));
        std::cout << "sdf: study IBL setup_ms="
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               start)
                         .count()
                  << '\n';
        scene_optics_ = host_buffer(16 * sizeof(MaterialOptics));
    }
    create_environment(device, gpu, config, slots);
    update_authored(scene);
    for (std::uint32_t i = 0; i < slots; ++i)
        preview_buffers_.push_back(host_buffer(authored_max_leaves * sizeof(SceneInstruction)));
    std::vector<vulkan::DescriptorSetBindingConfig> bindings{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        {14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT}};
    if (study) {
        bindings.push_back({7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT});
        for (std::uint32_t binding = 8; binding <= 12; ++binding)
            bindings.push_back(
                {binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT});
    }
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
        writes.storage_buffer(descriptors_->set(i), 5, preview_buffers_[i]->handle(),
                              preview_buffers_[i]->size());
        writes.storage_buffer(descriptors_->set(i), 6, scene_patterns_->handle(),
                              scene_patterns_->size());
        writes.storage_buffer(descriptors_->set(i), 13, scene_domains_->handle(),
                              scene_domains_->size());
        writes.storage_buffer(descriptors_->set(i), 14, scene_libraries_->handle(),
                              scene_libraries_->size());
        if (study_environment_) {
            writes.storage_buffer(descriptors_->set(i), 7, scene_optics_->handle(),
                                  scene_optics_->size());
            const auto textures = render::pbr_environment_texture_bindings(*study_environment_);
            writes.combined_image_sampler(descriptors_->set(i), 8, textures.irradiance_sampler,
                                          textures.irradiance_view, textures.irradiance_layout);
            writes.combined_image_sampler(descriptors_->set(i), 9, textures.prefiltered_sampler,
                                          textures.prefiltered_view, textures.prefiltered_layout);
            writes.combined_image_sampler(descriptors_->set(i), 10, textures.brdf_lut_sampler,
                                          textures.brdf_lut_view, textures.brdf_lut_layout);
        }
        writes.update(device);
        if (study_environment_)
            bind_environment(device, {i, slots});
    }
    const auto path = std::filesystem::path(CUBEY_SDF_SHADER_DIR);
    const std::array stages{render::vertex_shader_file(path / "sdf.vert.spv"),
                            render::fragment_shader_file(path / (study   ? "material_study.frag.spv"
                                                                 : scene ? "sdf_authored.frag.spv"
                                                                         : "sdf.frag.spv"))};
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
        profiler_.emplace(device, slots, material_atmosphere_ ? 16 : 4);
    if (profiler_ && material_atmosphere_)
        whole_profiler_.emplace(device, slots, 1);
    recorded_frames_.resize(slots);
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
    if (material_atmosphere_)
        std::cout << "sdf: environment finish sky_generation="
                  << atmosphere_.reflection_probe().snapshot().generation << " cloud_generation="
                  << (clouds_enabled_ ? atmosphere_.clouds().snapshot().generation : 0) << '\n';
    executor_.clear();
    profiler_.reset();
    whole_profiler_.reset();
    recorded_frames_.clear();
    post_.destroy();
    occluder_pipeline_.reset();
    pipeline_.reset();
    descriptors_.reset();
    scene_optics_.reset();
    study_environment_.reset();
    scene_materials_.reset();
    scene_patterns_.reset();
    scene_domains_.reset();
    scene_libraries_.reset();
    scene_program_.reset();
    preview_buffers_.clear();
    lighting_.reset();
    sky_background_.destroy();
    atmosphere_.destroy();
    atmosphere_textures_.reset();
    material_atmosphere_ = false;
    clouds_enabled_ = false;
    cloud_sky_only_ = false;
    cloud_sky_generation_ = 0;
    field_ = nullptr;
}
void Renderer::update_authored(const AuthoredScene* scene) {
    if (!scene_optics_ && scene &&
        std::any_of(scene->optics.begin(), scene->optics.end(),
                    [](const auto& o) { return !default_optics(o); }))
        throw std::invalid_argument("non-default material optics require material-study lighting");
    const auto bytes = authored_program_bytes(scene);
    scene_program_->upload(bytes.data(), bytes.size());
    if (scene_optics_) {
        const MaterialOptics fallback_optics;
        scene_optics_->upload(scene ? scene->optics.data() : &fallback_optics,
                              scene ? scene->optics.size() * sizeof(MaterialOptics)
                                    : sizeof(MaterialOptics));
    }
    std::array<math::Mat4, 16> domains;
    domains.fill(math::Mat4{1});
    if (scene && !scene->material_domains.empty())
        std::copy(scene->material_domains.begin(), scene->material_domains.end(), domains.begin());
    scene_domains_->upload(domains.data(), sizeof(domains));
    std::array<procedural::SurfaceLibrary, 16> libraries;
    if (scene && !scene->libraries.empty())
        std::copy(scene->libraries.begin(), scene->libraries.end(), libraries.begin());
    scene_libraries_->upload(libraries.data(), sizeof(libraries));
    const SurfaceMaterial fallback;
    const MaterialPattern plain;
    scene_materials_->upload(scene ? scene->materials.data() : &fallback,
                             scene ? scene->materials.size() * sizeof(SurfaceMaterial)
                                   : sizeof(SurfaceMaterial));
    scene_patterns_->upload(scene ? scene->patterns.data() : &plain,
                            scene ? scene->patterns.size() * sizeof(MaterialPattern)
                                  : sizeof(MaterialPattern));
}
void Renderer::record(vulkan::Device& device, VkCommandBuffer command,
                      render::ColorTargetView target, render::FrameSlot slot, bool present,
                      const SdfConfig& config, float yaw, float pitch, float distance,
                      profiling::ProfileRecorder* profile, std::uint64_t frame_index,
                      const BoxEdit* cutter, float cutter_opacity,
                      std::span<const SceneInstruction> previews) {
    if (profiler_) {
        profiler_->collect(slot.index);
        if (recorded_frames_[slot.index] && profile)
            for (const auto& timing : profiler_->latest_timings())
                profile->record_gpu_span(*recorded_frames_[slot.index], timing.label,
                                         timing.milliseconds);
        profiler_->begin_frame(command, slot.index);
    }
    if (whole_profiler_) {
        whole_profiler_->collect(slot.index);
        if (recorded_frames_[slot.index] && profile)
            for (const auto& timing : whole_profiler_->latest_timings())
                profile->record_gpu_span(*recorded_frames_[slot.index], timing.label,
                                         timing.milliseconds);
        whole_profiler_->begin_frame(command, slot.index);
    }
    if (profiler_)
        recorded_frames_[slot.index] = frame_index;
    const vulkan::GpuTimestampScope whole(whole_profiler_ ? &*whole_profiler_ : nullptr, command,
                                          slot.index, "sdf.whole_scene");
    const math::Vec3 target_point{0, config.target_y, 0};
    const math::Vec3 eye =
        target_point + math::Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                  std::cos(pitch) * std::cos(yaw)} *
                           distance;
    prepare_environment(device, command, slot, config, eye, target_point);
    const auto vp = math::perspective(glm::radians(45.0F),
                                      static_cast<float>(target.extent.width) /
                                          static_cast<float>(target.extent.height),
                                      0.01F, 100.0F) *
                    glm::lookAt(eye, target_point, math::Vec3{0, 1, 0});
    if (material_atmosphere_) {
        const auto forward = glm::normalize(target_point - eye),
                   right = glm::normalize(glm::cross(forward, math::Vec3{0, 1, 0}));
        sky_background_.upload(
            slot,
            atmosphere_
                .frame(
                    {.view_rays = {.right_aspect = {right,
                                                    static_cast<float>(scene_extent_.width) /
                                                        static_cast<float>(scene_extent_.height)},
                                   .up_tan_half_fovy = {glm::cross(right, forward),
                                                        std::tan(glm::radians(22.5F))},
                                   .forward = {forward, 0}}})
                .background);
    }
    const auto& light = atmosphere_.lighting();
    LightingUniforms uniforms{};
    uniforms.atmosphere = atmosphere_.frame({}).background;
    uniforms.direct_color = {light.primary_light_color * light.primary_light_intensity, 0};
    for (unsigned i = 0; i < 9; ++i)
        uniforms.irradiance[i] = {light.diffuse_irradiance_sh[i], 0};
    uniforms.material =
        config.clay ? math::Vec4{0.42F, 0.42F, 0.42F, 1} : math::Vec4{0.55F, 0.27F, 0.095F, 0};
    uniforms.quality = {config.roughness, config.shadow_softness, config.ao ? 1.0F : 0.0F, -0.48F};
    if (study_environment_ && !material_atmosphere_)
        uniforms.direct_color = {1.8F, 1.6F, 1.3F, 0};
    uniforms.surface_options = {config.procedural ? 1.0F : 0.0F, config.bump ? 1.0F : 0.0F,
                                config.material_fast_path ? 1.0F : 0.0F,
                                config.lighting == "material-furnace" ? 1.0F : 0.0F};
    const auto environment_bindings =
        study_environment_
            ? (material_atmosphere_ ? atmosphere_.pbr_environment_bindings(*study_environment_)
                                    : render::pbr_environment_texture_bindings(*study_environment_))
            : render::PbrEnvironmentTextureBindings{};
    uniforms.transport_options = {
        static_cast<float>(config.glass_crossings), environment_bindings.prefiltered_blend,
        static_cast<float>(environment_bindings.prefiltered_mip_levels
                               ? environment_bindings.prefiltered_mip_levels - 1
                               : 4),
        material_atmosphere_ ? 1.0F : 0.0F};
    if (config.lighting == "material-furnace")
        uniforms.direct_color = math::Vec4{0};
    if (cutter && config.cutter_cue && config.debug == 0) {
        uniforms.cutter = gpu_edit(*cutter);
        uniforms.cutter_style = {cutter_opacity, 0.0025F, 0, 0};
    }
    if (!previews.empty() && config.cutter_cue && config.debug == 0) {
        if (previews.size() > authored_max_leaves)
            throw std::invalid_argument("authored preview exceeds primitive capacity");
        preview_buffers_.at(slot.index)->upload(previews.data(), previews.size_bytes());
        uniforms.preview_style.x = cutter_opacity;
        uniforms.preview_style.y = static_cast<float>(previews.size());
    }
    lighting_->upload(slot, uniforms);
    const Camera camera{
        glm::inverse(vp),
        {eye, static_cast<float>(field_->atlas_axis)},
        {study_environment_ && !material_atmosphere_ ? glm::normalize(math::Vec3{-.35F, .42F, .84F})
                                                     : light.primary_light_direction,
         0},
        {static_cast<float>(config.debug), static_cast<float>(config.shadows),
         config.ground ? 1.0F : 0.0F, 0},
        {glm::normalize(target_point - eye),
         2.0F * std::tan(glm::radians(22.5F)) / static_cast<float>(scene_extent_.height)}};
    const bool raw_diagnostic = (config.debug > 0 && config.debug < 10) || config.debug == 12;
    post_.upload(slot, render::hdr_post_uniforms(
                           target.format, raw_diagnostic ? 0.0F : config.exposure,
                           raw_diagnostic ? render::PbrTonemap::Linear : render::PbrTonemap::Aces));
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
    const auto domains =
        builder.import_buffer({"sdf.domains", scene_domains_->size()}, scene_domains_->handle());
    const auto libraries = builder.import_buffer({"sdf.libraries", scene_libraries_->size()},
                                                 scene_libraries_->handle());
    const auto patterns =
        builder.import_buffer({"sdf.patterns", scene_patterns_->size()}, scene_patterns_->handle());
    const auto previews_buffer =
        builder.import_buffer({"sdf.previews", preview_buffers_.at(slot.index)->size()},
                              preview_buffers_.at(slot.index)->handle());
    builder.add_pass("sdf.trace", render::RenderGraphQueueDomain::Graphics)
        .read_texture(atlas, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(nodes, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_uniform_buffer(lighting, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(program, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(materials, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(patterns, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(domains, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(libraries, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        .read_storage_buffer(previews_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
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
                    if (material_atmosphere_ && config.debug == 0)
                        render::record_fullscreen_pipeline_draw(
                            pass, {.pipeline = &sky_background_.pipeline(),
                                   .descriptor_set = sky_background_.material().set(slot)});
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
    std::optional<render::CloudLayerRuntimeFrame> cloud_product;
    auto post_scene = scene;
    if (clouds_enabled_ && config.cloud_backdrop && config.debug == 0) {
        auto& clouds = atmosphere_.clouds();
        const auto frame = cloud_frame(eye, target_point);
        cloud_product = clouds.declare_surface_product(builder, slot, frame);
        post_scene = builder.create_texture(
            render::hdr_scene_color_texture_desc("sdf.clouded", scene_extent_, hdr_format));
        clouds.declare_surface_composite(builder, post_scene, *cloud_product, slot, scene, depth);
    }
    builder.add_pass("sdf.post", render::RenderGraphQueueDomain::Graphics)
        .read_texture(post_scene)
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
                         post_.update_scene_color_descriptor(device, slot, graph, resources,
                                                             post_scene);
                         if (cloud_product)
                             atmosphere_.clouds().update_surface_descriptors(
                                 device, slot, graph, resources, *cloud_product, scene, depth);
                     });
    if (cloud_product)
        atmosphere_.clouds().complete_surface_frame(slot, *cloud_product);
}
} // namespace cubey::projects::sdf
