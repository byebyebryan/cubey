#include "raytracing_forward.h"
#include "raytracing_materials.h"
#include "raytracing_renderer.h"
#include <algorithm>
#include <cubey/engine/material_recipe.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/render_graph.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <limits>

namespace cubey::projects::raytracing {
namespace {
Transform3D camera_transform(const CameraUniform& camera) {
    return {.translation = math::Vec3(camera.eye),
            .rotation = glm::quat_cast(math::Mat3(camera.inverse_view))};
}
} // namespace
ForwardComparison::ForwardComparison(vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                     const Scene& source, std::uint32_t slots, const Config& c)
    : device_(device), source_(source), diagnostic_(!c.hdr_output.empty()),
      transmission_source_(c.forward_transmission_source), debug_view_(c.view), recorded_(slots),
      thickness_(c.forward_thickness) {
    if (!source.optical_dragon || !slots)
        throw std::invalid_argument("forward comparison requires the certified dragon scene");
    environment_ = std::make_unique<Environment>(device, gpu, c, slots);
    textures_.emplace(render::create_pbr_default_texture_set(device, gpu));
    materials_.resize(slots);
    for (auto& table : materials_)
        table.initialize(device, {});
    std::array<asset::MaterialRecipe, 2> recipes{
        source.workshop->recipes.at(source.workshop->selected), *source.support_recipe};
    if (recipes[0].optics.settings.y <= 0 || recipes[0].library.optical_options.x != 0)
        throw std::invalid_argument("forward dragon comparison currently requires sharp glass");
    material_identity_ = nlohmann::json::array();
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        auto d = pbr_material_from_recipe(recipes[i], i == 0 ? thickness_ : .03F,
                                          static_cast<std::uint32_t>(i));
        // Render the entry surface once. The shared volume shader estimates
        // the exit itself; shading back faces as another layer doubles glass.
        d.cull_mode = VK_CULL_MODE_BACK_BIT;
        d.factors.pattern_enabled = c.pattern;
        d.factors.bump_enabled = c.bump;
        const auto h = engine_.render_resources().create_material(render::pbr_material_info(d));
        material_handles_.push_back(h);
        for (auto& table : materials_)
            (void)table.emplace(h, d, render::pbr_default_sampled_image_bindings(*textures_));
        material_identity_.push_back(
            {{"id", recipes[i].id},
             {"fingerprint", asset::material_recipe_fingerprint(recipes[i])},
             {"transmission_factor", d.factors.transmission_factor},
             {"ior", d.factors.dielectric_ior},
             {"roughness", d.factors.roughness_factor},
             {"thickness", d.factors.volume_thickness_factor},
             {"attenuation_color",
              {d.factors.volume_attenuation_color.r, d.factors.volume_attenuation_color.g,
               d.factors.volume_attenuation_color.b}},
             {"attenuation_distance", d.factors.volume_attenuation_distance},
             {"cull_mode", "back"}});
    }
    for (const auto& mesh : source.meshes) {
        std::vector<render::PbrVertex> vertices;
        vertices.reserve(mesh.vertices.size());
        for (const auto& v : mesh.vertices)
            vertices.push_back(forward_vertex(v));
        const auto handle = engine_.render_resources().create_mesh("dragon forward comparison");
        meshes_.emplace(handle, gpu,
                        render::indexed_mesh_config(std::span<const render::PbrVertex>(vertices),
                                                    std::span<const std::uint32_t>(mesh.indices)));
        mesh_handles_.push_back(handle);
    }
    scene_ = &engine_.create_scene();
    auto setup = scene_->begin_transaction();
    for (const auto& instance : source.instances) {
        const auto& mesh = source.meshes.at(instance.mesh);
        // The authored scene uses translation and axis scale only.
        const Transform3D transform{.translation = math::Vec3(instance.transform[3]),
                                    .scale = {glm::length(math::Vec3(instance.transform[0])),
                                              glm::length(math::Vec3(instance.transform[1])),
                                              glm::length(math::Vec3(instance.transform[2]))}};
        math::Vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (const auto& v : mesh.vertices) {
            lo = glm::min(lo, math::Vec3(v.position));
            hi = glm::max(hi, math::Vec3(v.position));
        }
        const bool body = instance.recipe_role == Instance::RecipeRole::Selected;
        const auto e = scene::create_renderable_entity_3d(
            setup, {.transform = transform,
                    .mesh = mesh_handles_.at(instance.mesh),
                    .material = material_handles_.at(body ? 0 : 1),
                    .local_bounds = {.center = (lo + hi) * .5F, .half_extent = (hi - lo) * .5F},
                    .cast_shadows = false,
                    .receive_shadows = false});
        if (body)
            dragon_ = e;
        triangles_ += mesh.indices.size() / 3;
    }
    camera_ = scene::create_camera_entity_3d(
        setup, {}, Camera3D({.fovy_radians = glm::radians(c.fov), .near_z = .01F, .far_z = 100}));
    auto light = directional_light_3d({0, 1, 0}, {1, 1, 1}, 1);
    light.casts_shadows = false;
    light_ = scene::create_directional_light_entity_3d(setup, light);
    setup.commit();
    renderer_ =
        std::make_unique<ForwardPbrRenderer3D>(forward_pbr_renderer_3d_config_from_shader_directory(
            std::filesystem::path(CUBEY_RAYTRACING_SHADER_DIR) / "forward",
            {.scene_color_format =
                 diagnostic_ ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT}));
    renderer_->create_global_resources(
        device, {.environment_textures = environment_->shared_atmosphere().pbr_environment_bindings(
                     environment_->fallback(), false),
                 .frame_slot_count = slots,
                 .atmosphere_background_textures = environment_->background_bindings()});
    timer_.emplace(device, slots, 1);
}
ForwardComparison::~ForwardComparison() {
    if (renderer_)
        renderer_->destroy_all_resources();
    if (scene_)
        engine_.destroy_scene(*scene_);
    for (auto& h : mesh_handles_)
        render::destroy_mesh_resource(engine_.render_resources(), meshes_, h);
    materials_.clear();
    for (auto h : material_handles_)
        engine_.render_resources().destroy_material(h);
}
void ForwardComparison::resize(VkExtent2D extent, VkFormat format) {
    if (extent.width == extent_.width && extent.height == extent_.height && format == format_)
        return;
    finish();
    renderer_->destroy_swapchain_resources();
    renderer_->create_swapchain_resources(
        device_, {.extent = extent, .color_format = format, .materials = &materials_.front()});
    extent_ = extent;
    format_ = format;
    if (diagnostic_)
        hdr_readback_.emplace(device_, vulkan::readback_buffer_config(VkDeviceSize{extent.width} *
                                                                      extent.height * 16U));
    recorded_hdr_ = false;
}
void ForwardComparison::collect(std::uint32_t slot) {
    if (!recorded_[slot])
        return;
    timer_->collect(slot);
    for (const auto& t : timer_->latest_timings())
        if (timings_.size() < 20000)
            timings_.push_back(
                {{"frame", *recorded_[slot]}, {"pass", t.label}, {"milliseconds", t.milliseconds}});
    recorded_[slot].reset();
}
void ForwardComparison::finish() {
    for (std::uint32_t i = 0; i < recorded_.size(); ++i)
        collect(i);
}
void ForwardComparison::render(VkCommandBuffer command, render::ColorTargetView output,
                               render::FrameSlot slot, const Config& c, bool present,
                               profiling::ProfileRecorder*, std::uint64_t frame) {
    collect(slot.index);
    vulkan::CommandRecorder recorder(command);
    if (present)
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    timer_->begin_frame(command, slot.index);
    timer_->begin_pass(command, slot.index, "forward.total_frame");
    const auto camera = camera_uniform(c, extent_);
    auto sun = math::Vec3(camera.sun);
    const auto lighting = environment_->prepare(c, command, slot, math::Vec3(camera.eye), sun);
    auto edits = scene_->create_edit_queue();
    edits.transforms3d().set_local_transform(camera_, camera_transform(camera));
    scene_->commit(edits);
    auto view = scene_->read();
    scene::Environment3D env{
        .ambient_color = {0, 0, 0}, .ambient_intensity = 0, .diffuse_irradiance_sh_enabled = true};
    for (std::size_t i = 0; i < 9; ++i)
        env.diffuse_irradiance_sh[i] = math::Vec3(lighting.irradiance[i]);
    auto plan = scene::build_render_frame_plan_3d({.camera_entity = camera_,
                                                   .width = extent_.width,
                                                   .height = extent_.height,
                                                   .environment = env,
                                                   .culling_enabled = false},
                                                  view, engine_.render_resources());
    // Override the static light packet with the exact shared RT light state.
    for (auto& light : plan.light_packets) {
        light.direction = sun;
        light.color = math::Vec3(lighting.direct_color);
        light.intensity = 1;
        light.casts_shadows = false;
    }
    if (c.hide_dragon)
        std::erase_if(plan.draw_packets,
                      [&](const auto& packet) { return packet.entity == dragon_; });
    const scene::FrameRenderPlan3D plans(
        {{.label = "shadow", .kind = scene::RenderPassKind3D::DepthOnly, .frame_plan = plan},
         {.label = "scene", .kind = scene::RenderPassKind3D::Color, .frame_plan = plan}});
    render::ViewRayBasis3D rays{{math::Vec3(camera.inverse_view[0]), camera.eye.w},
                                {math::Vec3(camera.inverse_view[1]), camera.light.z},
                                {-math::Vec3(camera.inverse_view[2]), 0}};
    auto settings = ForwardPbrRenderer3DSettings{
        .exposure = c.exposure,
        .debug_view =
            render::pbr_debug_view_from_name(c.view == "shading-normal" ? "normal" : c.view),
        .background_mode = ForwardPbrRenderer3DBackgroundMode::Atmosphere,
        .atmosphere_background =
            environment_->shared_atmosphere().frame({.view_rays = rays}).background};
    settings.transmission_source = c.forward_transmission_source == "hybrid"
                                       ? ForwardPbrTransmissionSource::Hybrid
                                   : c.forward_transmission_source == "environment"
                                       ? ForwardPbrTransmissionSource::Environment
                                       : ForwardPbrTransmissionSource::Screen;
    transmission_source_ = c.forward_transmission_source;
    debug_view_ = c.view;
    renderer_->update_environment_frame(
        device_, slot,
        render::pbr_environment_frame_bindings(
            environment_->shared_atmosphere().pbr_environment_bindings(environment_->fallback(),
                                                                       false)));
    renderer_->record(
        {.target = {.device = &device_,
                    .command_buffer = command,
                    .color_target = output,
                    .frame_slot = slot,
                    .color_initial_state =
                        present ? render::render_graph_undefined_texture_state()
                                : render::render_graph_color_attachment_texture_state(),
                    .color_final_state =
                        present ? render::render_graph_present_texture_state()
                                : render::render_graph_color_attachment_texture_state(),
                    .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording},
         .view = {.scene = &view,
                  .frame_plan = &plans,
                  .camera_entity = camera_,
                  .light_entity = light_},
         .scene_resources = {.meshes = &meshes_, .materials = &materials_.at(slot.index)},
         .settings = settings,
         .scene_readback = hdr_readback_ ? std::optional{ForwardPbrRenderer3DSceneReadback{
                                               .buffer = hdr_readback_->handle(),
                                               .byte_size = hdr_readback_->size()}}
                                         : std::nullopt});
    recorded_hdr_ = bool(hdr_readback_);
    timer_->end_pass(command, slot.index);
    recorded_[slot.index] = frame;
    if (present)
        recorder.end("forward dragon comparison");
}
void ForwardComparison::save_hdr(const std::filesystem::path& path) const {
    if (!hdr_readback_ || !recorded_hdr_)
        throw std::runtime_error("no completed forward HDR capture");
    std::vector<std::byte> bytes(static_cast<std::size_t>(hdr_readback_->size()));
    hdr_readback_->download(bytes.data(), hdr_readback_->size());
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw std::runtime_error("cannot write forward HDR capture");
}
nlohmann::json ForwardComparison::metrics() const {
    return {{"format", "cubey.raytracing.forward-comparison.metrics"},
            {"renderer", "forward"},
            {"ray_tracing_enabled", device_.ray_tracing_enabled()},
            {"as_bytes", 0},
            {"scene_upload_and_build_ms", nullptr},
            {"pipeline_setup_ms", nullptr},
            {"triangles", triangles_},
            {"extent", {extent_.width, extent_.height}},
            {"forward_thickness", thickness_},
            {"forward_transmission_source", transmission_source_},
            {"view", debug_view_},
            {"hdr_output_format", diagnostic_ ? "RGBA32F" : "none"},
            {"spatial_samples", 1},
            {"transmission",
             transmission_source_ == "hybrid"
                 ? "shared estimated exit; opaque scene coverage plus outgoing environment"
             : transmission_source_ == "environment"
                 ? "shared solid-sphere estimated exit; environment along outgoing direction"
                 : "shared solid-sphere estimated exit; screen HDR plus environment fallback"},
            {"reflection", "shared cached environment PBR"},
            {"materials", material_identity_},
            {"geometry_admission", mesh_admission_identity(source_.meshes.front())},
            {"gpu_samples", timings_}};
}
} // namespace cubey::projects::raytracing
