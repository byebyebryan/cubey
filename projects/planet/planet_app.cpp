#include "planet_app.h"
#include "planet_surface_product.h"

#include <cubey/core/jobs.h>
#include <cubey/engine/staged_resource.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <cubey/procedural/artifact_cache.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/atmosphere_environment.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/target.h>
#include <cubey/render/texture.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>

#include <imgui.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_PLANET_SHADER_DIR
#error "CUBEY_PLANET_SHADER_DIR must be defined by the planet CMake target"
#endif

namespace cubey::projects::planet {
namespace {

constexpr float kPlanetRadius = 1.0F;
constexpr std::uint32_t kSurfaceBinding = 0U;
constexpr std::uint32_t kSurfaceFieldsBinding = 1U;
constexpr std::uint32_t kSurfaceExtent = 512U;

struct PlanetOrbitUniforms {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 camera_position{0.0F, 0.0F, 3.2F, 1.0F};
    cubey::math::Vec4 sun_direction_intensity{0.5F, 0.4F, 0.75F, 1.0F};
    cubey::math::Vec4 surface_options{0.35F, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(PlanetOrbitUniforms) == sizeof(float) * 4U * 7U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PLANET_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::MaterialPassInfo planet_orbit_pass_info() {
    return {
        .label = "planet.orbit.surface",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0U,
                    .bindings =
                        {
                            {.binding = kSurfaceBinding,
                             .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .stage_flags = VK_SHADER_STAGE_VERTEX_BIT |
                                            VK_SHADER_STAGE_FRAGMENT_BIT},
                            {.binding = kSurfaceFieldsBinding,
                             .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT},
                        },
                },
            },
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] cubey::render::TextureCube create_placeholder_surface_texture(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
    const std::array<std::uint8_t, 24> texels{
        70U,  0U,   0U,   150U, 70U,  0U,   0U,   150U,
        70U,  0U,   0U,   150U, 70U,  0U,   0U,   150U,
        70U,  0U,   0U,   150U, 70U,  0U,   0U,   150U,
    };
    return cubey::render::create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = 1U,
            .mip_levels = 1U,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .bytes = texels,
            .create_sampler = true,
            .sampler =
                {
                    .min_filter = VK_FILTER_LINEAR,
                    .mag_filter = VK_FILTER_LINEAR,
                    .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                },
        });
}

[[nodiscard]] cubey::render::TextureCube create_surface_texture(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuOwnerContext& gpu,
    PlanetSurfaceProduct product) {
    return cubey::render::create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = product.config.extent,
            .mip_levels = 1U,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .bytes = product.rgba,
            .create_sampler = true,
            .sampler =
                {
                    .min_filter = VK_FILTER_LINEAR,
                    .mag_filter = VK_FILTER_LINEAR,
                    .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                },
        });
}

[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv>
make_orbit_sphere_mesh() {
    return cubey::render::make_uv_sphere_position_color_normal_uv_mesh(
        {.radius = kPlanetRadius, .latitude_segments = 128U, .longitude_segments = 256U});
}

[[nodiscard]] cubey::render::VertexInputLayout orbit_vertex_input_layout() {
    return cubey::render::vertex_position_color_normal_uv_input_layout();
}

[[nodiscard]] float phase_degrees_for_orbital_view(
    const std::optional<PlanetOrbitalView>& view) {
    if (!view.has_value() || view.value() == PlanetOrbitalView::Lit) {
        return 0.0F;
    }
    if (view.value() == PlanetOrbitalView::Terminator) {
        return 90.0F;
    }
    if (view.value() == PlanetOrbitalView::Crescent) {
        return 135.0F;
    }
    if (view.value() == PlanetOrbitalView::Night) {
        return 170.0F;
    }
    throw std::invalid_argument("unknown planet orbital view");
}

[[nodiscard]] float distance_for_disk_coverage(float coverage, float fovy_radians) {
    const float apparent_tangent = coverage * std::tan(fovy_radians * 0.5F);
    return std::sqrt(1.0F + 1.0F / (apparent_tangent * apparent_tangent));
}

class PlanetApp {
  public:
    explicit PlanetApp(PlanetConfig config)
        : config_(std::move(config)),
          surface_jobs_(1U),
          surface_builds_(surface_jobs_),
          surface_cache_({.root = cubey::procedural::default_procedural_artifact_cache_root()}) {
        validate_planet_config(config_);
        if (config_.planet.surface_quality == PlanetSurfaceQuality::Draft) {
            surface_config_.extent = 256U;
        }
        if (config_.planet.terrain_seed.has_value()) {
            surface_config_.seed = config_.planet.terrain_seed.value();
        }
        phase_degrees_ = phase_degrees_for_orbital_view(config_.planet.orbital_view);
        debug_view_ = static_cast<int>(resolve_planet_debug_view(config_.common.debug_view));
        headless_base_phase_degrees_ = phase_degrees_;
        const float initial_distance = config_.planet.disk_coverage.has_value()
                                           ? distance_for_disk_coverage(
                                                 config_.planet.disk_coverage.value(),
                                                 camera_.fovy_radians())
                                           : 5.4F;
        orbit_controller_.set_home_distance(initial_distance);
        orbit_controller_.set_distance(initial_distance);
        orbit_controller_.set_distance_limits(1.5F, 12.0F);
        orbit_controller_.set_pitch_limits(-1.35F, 1.35F);
        camera_transform_ = cubey::orbit_camera_transform(
            {.target = {0.0F, 0.0F, 0.0F},
             .distance = orbit_controller_.distance(),
             .yaw = orbit_controller_.yaw(),
             .pitch = orbit_controller_.pitch()});
    }

    PlanetApp(const PlanetApp&) = delete;
    PlanetApp& operator=(const PlanetApp&) = delete;

    int run() {
        if (config_.common.headless) {
            return run_headless();
        }
        return run_windowed();
    }

  private:
    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources(context.device(), context.gpu(), context.frame_slot_count());
            request_surface_product();
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_swapchain_resources(context.device(), context.swapchain().extent(),
                                       context.swapchain().format());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) {
            orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
            if (context.filtered_input().key_pressed(cubey::input::Key::R)) {
                orbit_controller_.reset();
            }
            if (time_playing_) {
                phase_degrees_ = std::fmod(phase_degrees_ +
                                               static_cast<float>(timing.delta_seconds) * 5.0F,
                                           360.0F);
            }
            update_camera_transform();
            poll_surface_product(context.device(), context.gpu());
        };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext&) { draw_ui(); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext&,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_frame(frame.command_buffer, frame.color_target, frame.frame_slot, true);
        };
        callbacks.frame_stats_sample = [](cubey::host::WindowedAppContext& context,
                                          const FrameTiming& timing) {
            return cubey::host::FrameStatsSample{
                .delta_seconds = timing.delta_seconds,
                .width = context.swapchain().extent().width,
                .height = context.swapchain().extent().height,
                .triangles = 128U * 256U * 2U,
            };
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
            destroy_all_resources(context.gpu());
        };
        return cubey::host::run_windowed_app(
            {.run_config = config_.common,
             .app_name = "planet",
             .ready_status = "rendering orbital planet project",
             .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
             .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
             .require_dynamic_rendering = true,
             .close_on_escape = true},
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_.common;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            create_global_resources(context.device(), context.gpu(),
                                    cubey::host::headless_capture_frame_slot_count(config_.common));
            request_surface_product();
            surface_builds_.finish(context.gpu());
            install_ready_surface_product(context.device());
            create_swapchain_resources(context.device(), context.render_target().extent,
                                       context.render_target().format);
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext&,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            phase_degrees_ = headless_base_phase_degrees_;
            if (config_.common.capture_mode == CaptureMode::Video && time_playing_) {
                phase_degrees_ += static_cast<float>(frame.timing.elapsed_seconds) * 5.0F;
            }
            update_camera_transform();
            record_frame(command_buffer, target, frame.frame_slot, false);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext& context) {
            destroy_all_resources(context.gpu());
        };
        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void create_global_resources(const cubey::vulkan::Device& device,
                                 cubey::vulkan::GpuRuntime& gpu,
                                 std::uint32_t frame_slot_count) {
        if (surface_material_.has_value()) {
            return;
        }
        surface_texture_.emplace(create_placeholder_surface_texture(device, gpu));
        atmosphere_textures_.emplace(
            cubey::render::create_atmosphere_background_cached_textures(device, gpu));
        atmosphere_frame_.create_materials(
            device,
            {.frame_slot_count = frame_slot_count, .textures = atmosphere_textures_->bindings()});
        surface_material_.emplace(
            device,
            cubey::render::FrameUniformMaterialInstanceConfig{
                .material_pass = planet_orbit_pass_info(),
                .descriptor_set = 0U,
                .frame_slot_count = frame_slot_count,
                .uniform_binding = kSurfaceBinding,
                .sampled_images =
                    {{.binding = kSurfaceFieldsBinding,
                      .sampler = surface_texture_->sampler().handle(),
                      .image_view = surface_texture_->view()}},
            });
        const auto mesh_data = make_orbit_sphere_mesh();
        surface_mesh_.emplace(gpu, mesh_data.mesh_config());
    }

    void create_swapchain_resources(const cubey::vulkan::Device& device, VkExtent2D extent,
                                    VkFormat color_format) {
        const std::array atmosphere_shaders{
            cubey::render::vertex_shader_file(
                std::filesystem::path(CUBEY_PLANET_SHADER_DIR) / "atmosphere.vert.spv"),
            cubey::render::fragment_shader_file(
                std::filesystem::path(CUBEY_PLANET_SHADER_DIR) / "atmosphere.frag.spv"),
        };
        atmosphere_frame_.create_pipeline(
            device, {.extent = extent, .color_format = color_format, .shader_stage_files = atmosphere_shaders});
        const std::array orbit_shaders{
            cubey::render::vertex_shader_file(shader_path("planet_orbit.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("planet_orbit.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input = orbit_vertex_input_layout();
        const std::array descriptor_set_layouts{surface_material().layout()};
        surface_pass_.emplace(
            device,
            cubey::render::GraphicsPipelineTargetInfo{.extent = extent, .color_format = color_format},
            cubey::render::ForwardScenePass3DConfig{
                .pipeline = {.shader_stage_files = orbit_shaders,
                             .vertex_bindings = vertex_input.bindings(),
                             .vertex_attributes = vertex_input.attribute_descriptions(),
                             .descriptor_set_layouts = descriptor_set_layouts,
                             .material_pass = planet_orbit_pass_info()},
                .clear = {.color = cubey::render::color_clear_value(0.0F, 0.0F, 0.0F, 1.0F),
                          .depth = cubey::render::depth_clear_value()},
            });
    }

    void destroy_swapchain_resources() {
        surface_pass_.reset();
        atmosphere_frame_.destroy_pipeline();
    }

    void destroy_all_resources(cubey::vulkan::GpuRuntime& gpu) {
        destroy_swapchain_resources();
        surface_builds_.shutdown(gpu);
        surface_jobs_.shutdown();
        surface_mesh_.reset();
        surface_material_.reset();
        atmosphere_frame_.destroy();
        atmosphere_textures_.reset();
        surface_texture_.reset();
    }

    void request_surface_product() {
        static_cast<void>(surface_builds_.request(
            "planet orbital surface", [this] { return prepare_planet_surface_product(surface_cache_, surface_config_); },
            [this](cubey::vulkan::GpuOwnerContext& context, PlanetSurfaceProduct&& product) {
                source_cache_hit_ = product.cache_hit;
                source_content_hash_ = product.content_hash;
                return create_surface_texture(context.device(), context, std::move(product));
            }));
    }

    void poll_surface_product(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu) {
        static_cast<void>(surface_builds_.poll(gpu));
        if (surface_builds_.ready()) {
            install_ready_surface_product(device);
        }
    }

    void install_ready_surface_product(const cubey::vulkan::Device& device) {
        if (!surface_builds_.ready()) {
            return;
        }
        surface_texture_.emplace(std::move(surface_builds_.take_ready().resident));
        for (std::uint32_t index = 0U; index < surface_material().material().set_count(); ++index) {
            const cubey::render::FrameSlot slot{.index = index,
                                                .count = surface_material().material().set_count()};
            cubey::render::MaterialDescriptorWriter writer(surface_material().set(slot));
            writer.uniform_buffer(kSurfaceBinding,
                                  surface_material().uniforms().buffer(slot).handle(),
                                  surface_material().uniforms().range());
            writer.combined_image_sampler(kSurfaceFieldsBinding, surface_texture_->sampler().handle(),
                                          surface_texture_->view());
            writer.update(device);
        }
    }

    void update_camera_transform() {
        camera_transform_ = cubey::orbit_camera_transform(
            {.target = {0.0F, 0.0F, 0.0F},
             .distance = orbit_controller_.distance(),
             .yaw = orbit_controller_.yaw(),
             .pitch = orbit_controller_.pitch()});
    }

    [[nodiscard]] cubey::render::CelestialSystem celestial_system() const {
        cubey::render::CelestialSolarTime time{.day_of_year = 172.0F,
                                                .time_hours = 12.0F,
                                                .hours_per_second = 0.0F};
        cubey::render::CelestialSystem celestial = cubey::render::celestial_system_from_solar_time(time);
        const float phase = glm::radians(phase_degrees_);
        celestial.sun.direction = glm::normalize(cubey::math::Vec3{
            std::sin(phase), 0.38F, -std::cos(phase)});
        return celestial;
    }

    [[nodiscard]] PlanetOrbitUniforms orbit_uniforms(VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const cubey::render::CelestialSystem celestial = celestial_system();
        const cubey::math::Vec3 camera_position = camera_transform_.translation;
        return {
            .view_projection = camera_.view_projection_matrix(camera_transform_, aspect),
            .camera_position = {camera_position.x, camera_position.y, camera_position.z, 1.0F},
            .sun_direction_intensity = {celestial.sun.direction.x, celestial.sun.direction.y,
                                        celestial.sun.direction.z, celestial.sun.intensity},
            .surface_options = {cloud_coverage_, static_cast<float>(debug_view_), 0.0F, 0.0F},
        };
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms atmosphere_uniforms(
        VkExtent2D extent) const {
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        cubey::render::AtmosphereEnvironmentConfig atmosphere{};
        atmosphere.ground_mode = cubey::render::AtmosphereEnvironmentGroundMode::SkyOnlyNoGroundOcclusion;
        atmosphere.reference_geometry_enabled = false;
        atmosphere.camera_altitude_km = 450.0F;
        atmosphere.render_celestial_content = true;
        return cubey::render::atmosphere_environment_frame_uniforms_from_celestial(
            atmosphere, celestial_system(),
            {.view_rays = cubey::render::view_ray_basis_3d(camera_transform_.rotation, aspect,
                                                            camera_.fovy_radians()),
             .camera_position_km = {0.0F, 0.0F, atmosphere.bottom_radius_km + atmosphere.camera_altitude_km},
             .camera_position_km_explicit = true});
    }

    void record_frame(VkCommandBuffer command_buffer, cubey::render::ColorTargetView target,
                      cubey::render::FrameSlot frame_slot, bool present) {
        surface_material().upload(frame_slot, orbit_uniforms(target.extent));
        atmosphere_frame_.upload(frame_slot, atmosphere_uniforms(target.extent));
        const cubey::vulkan::CommandRecorder recorder(command_buffer);
        if (present) {
            recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        } else {
            recorder.transition_image_layout(cubey::vulkan::begin_depth_attachment_transition(
                surface_pass().depth_attachment().handle()));
        }
        atmosphere_frame_.record_pass(recorder, target, frame_slot);
        const cubey::render::RenderTargetAttachmentOps ops{
            .color = cubey::vulkan::load_store_attachment_ops(),
            .depth = cubey::vulkan::clear_store_attachment_ops(),
        };
        const cubey::render::RenderTargetRenderingInfo rendering(surface_pass().target(target),
                                                                  surface_pass().clear_values(), ops);
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(target.extent);
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, surface_pass().pipeline().pipeline());
        cubey::render::bind_material_instance(recorder, surface_pass().pipeline(),
                                              surface_material().material(), frame_slot);
        cubey::render::record_draw_item(recorder.handle(), {.mesh = &surface_mesh()});
        recorder.end_rendering();
        if (present) {
            recorder.end("vkEndCommandBuffer planet");
        }
    }

    void draw_ui() {
        ImGui::Begin("Planet");
        ImGui::TextUnformatted("Orbital V1");
        ImGui::Separator();
        ImGui::Text("Surface: %s", cubey::staged_resource_phase_name(surface_builds_.status().phase).data());
        ImGui::Text("Source: %s", source_cache_hit_ ? "cache" : "generated");
        ImGui::Text("Cubemap: %u px / face", surface_config_.extent);
        ImGui::Text("Seed: %llu", static_cast<unsigned long long>(surface_config_.seed));
        ImGui::Text("Triangles: %u", 128U * 256U * 2U);
        ImGui::SliderFloat("Cloud coverage", &cloud_coverage_, 0.0F, 1.0F);
        constexpr const char* debug_view_labels[] = {"Final", "Land", "Elevation", "Ice",
                                                     "Roughness", "Albedo"};
        ImGui::Combo("Surface debug", &debug_view_, debug_view_labels,
                     static_cast<int>(std::size(debug_view_labels)));
        ImGui::SliderFloat("Disk distance", &ui_distance_, 1.5F, 12.0F);
        if (std::abs(ui_distance_ - orbit_controller_.distance()) > 0.001F) {
            orbit_controller_.set_distance(ui_distance_);
        } else {
            ui_distance_ = orbit_controller_.distance();
        }
        ImGui::SliderFloat("Phase", &phase_degrees_, 0.0F, 180.0F, "%.0f deg");
        ImGui::Checkbox("Animate phase", &time_playing_);
        if (ImGui::Button("Lit")) {
            phase_degrees_ = 0.0F;
        }
        ImGui::SameLine();
        if (ImGui::Button("Terminator")) {
            phase_degrees_ = 90.0F;
        }
        ImGui::SameLine();
        if (ImGui::Button("Crescent")) {
            phase_degrees_ = 135.0F;
        }
        ImGui::SameLine();
        if (ImGui::Button("Night")) {
            phase_degrees_ = 170.0F;
        }
        ImGui::End();
    }

    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<PlanetOrbitUniforms>&
    surface_material() const {
        if (!surface_material_.has_value()) {
            throw std::runtime_error("planet surface material is not initialized");
        }
        return surface_material_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& surface_pass() const {
        if (!surface_pass_.has_value()) {
            throw std::runtime_error("planet surface pass is not initialized");
        }
        return surface_pass_.value();
    }

    [[nodiscard]] const cubey::render::Mesh& surface_mesh() const {
        if (!surface_mesh_.has_value()) {
            throw std::runtime_error("planet surface mesh is not initialized");
        }
        return surface_mesh_.value();
    }

    PlanetConfig config_;
    cubey::Camera3D camera_{
        {.fovy_radians = glm::radians(42.0F), .near_z = 0.01F, .far_z = 20.0F}};
    cubey::Transform3D camera_transform_{};
    cubey::OrbitController orbit_controller_{};
    float ui_distance_ = 5.4F;
    float phase_degrees_ = 35.0F;
    float headless_base_phase_degrees_ = 35.0F;
    float cloud_coverage_ = 0.0F;
    int debug_view_ = 0;
    bool time_playing_ = true;

    PlanetSurfaceProductConfig surface_config_{.extent = kSurfaceExtent, .seed = 1337U};
    cubey::jobs::JobSystem surface_jobs_;
    cubey::StagedResource<PlanetSurfaceProduct, cubey::render::TextureCube> surface_builds_;
    cubey::procedural::ProceduralArtifactCache surface_cache_;
    bool source_cache_hit_ = false;
    std::uint64_t source_content_hash_ = 0U;

    std::optional<cubey::render::TextureCube> surface_texture_;
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> atmosphere_textures_;
    cubey::render::AtmosphereBackgroundFrame atmosphere_frame_;
    std::optional<cubey::render::FrameUniformMaterialInstance<PlanetOrbitUniforms>> surface_material_;
    std::optional<cubey::render::Mesh> surface_mesh_;
    std::optional<cubey::render::ForwardScenePass3D> surface_pass_;
};

} // namespace

int run_planet(const PlanetConfig& config) {
    PlanetApp app(config);
    return app.run();
}

} // namespace cubey::projects::planet
