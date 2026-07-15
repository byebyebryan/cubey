#include "planet_app.h"

#include "planet_atmosphere_adapter.h"
#include "planet_camera.h"
#include "planet_celestial.h"
#include "planet_config.h"
#include "planet_frame.h"
#include "planet_local_detail_runtime.h"
#include "planet_surface.h"
#include "planet_surface_runtime.h"
#include "planet_ui.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/math.h>
#include <cubey/engine/atmosphere_environment_config.h>
#include <cubey/engine/cloud_environment_config.h>
#include <cubey/host/frame_stats.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/render/atmosphere_background_frame.h>
#include <cubey/render/cloud_layer.h>
#include <cubey/render/forward_pass.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/instance_buffer.h>
#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pass.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/render_graph.h>
#include <cubey/render/view_ray_basis_3d.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/vk_check.h>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_PLANET_SHADER_DIR
#error "CUBEY_PLANET_SHADER_DIR must be defined by the planet target"
#endif

namespace cubey::projects::planet {
namespace {

constexpr const char* kAppName = "planet";
constexpr const char* kReadyStatus = "rendering planet project";
constexpr float kPlanetCameraBaseYaw = 0.55F;
constexpr float kPlanetCameraBasePitch = 0.28F;
constexpr float kPlanetMoonAngularRadiusScale = 4.0F;
constexpr float kPlanetMoonShellDistanceFraction = 0.88F;
constexpr float kPlanetSurfaceBaseLodTargetEdgePx = 14.0F;
constexpr float kPlanetLocalDetailInspectionMinimumOuterHalfExtentM = 131072.0F;
constexpr float kPlanetLocalDetailInspectionHorizonExtentScale = 1.35F;
constexpr float kPlanetSharedAtmosphereSunRadiance = 22.0F;
constexpr float kPlanetSharedAtmosphereMinTwilightSoftness = 0.022F;
constexpr float kPlanetCloudSceneDepthFadeM = 500.0F;
constexpr std::uint32_t kPlanetSurfaceFrameUniformBinding = 0;
constexpr VkFormat kPlanetSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

struct PlanetSurfaceFrameUniforms {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 light_direction_debug{0.35F, 0.78F, 0.50F, 0.0F};
    cubey::math::Vec4 render_origin_radius{0.0F, 0.0F, 0.0F, kPlanetDefaultRadiusM};
    cubey::math::Vec4 surface_options{0.0F, 0.0F, 1.0F, 0.0F};
    cubey::math::Vec4 terrain_options{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 field_options{0.0F, kPlanetDefaultBathymetryDepthScaleM,
                                    kPlanetDefaultShorelineWidthM, 0.0F};
    cubey::math::Vec4 camera_horizon{0.0F, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 atmosphere_options{0.14F, 0.42F, 0.45F, 1.0F};
    cubey::math::Vec4 haze_color_direct{0.18F, 0.28F, 0.44F, 0.86F};
    cubey::math::Vec4 celestial_equator_plane{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 celestial_ecliptic_plane{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 celestial_moon_orbit_plane{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 celestial_sun_direction{0.0F, 1.0F, 0.0F, 0.0F};
    cubey::math::Vec4 celestial_moon_direction{0.0F, 0.0F, 1.0F, 0.0F};
    cubey::math::Vec4 camera_world_radius{0.0F, 0.0F, 0.0F,
                                          kPlanetDefaultRadiusM + kPlanetDefaultCameraAltitudeM};
    cubey::math::Vec4 atmosphere_radius_mode{
        kPlanetDefaultRadiusM + kPlanetDefaultAtmosphereHeightM, kPlanetDefaultAtmosphereHeightM,
        static_cast<float>(static_cast<std::uint32_t>(PlanetAtmosphereMode::Physical)), 0.004675F};
    cubey::math::Vec4 atmosphere_rayleigh{0.005802F, 0.013558F, 0.033100F, 8.0F};
    cubey::math::Vec4 atmosphere_mie{0.003996F, 0.004400F, 1.2F, 0.80F};
    cubey::math::Vec4 atmosphere_ozone{0.000650F, 0.001881F, 0.000085F, 25.0F};
    cubey::math::Vec4 atmosphere_shared_options{15.0F, 22.0F, 0.022F, 0.0F};
    cubey::math::Vec4 sun_color_intensity{1.0F, 0.94F, 0.82F, 0.88F};
    cubey::math::Vec4 moon_color_intensity{cubey::render::kCelestialMoonLightColor, 0.0F};
    cubey::math::Vec4 local_origin_options{0.0F, 0.0F, 0.0F, 0.0F};
    cubey::math::Vec4 local_right_outer{1.0F, 0.0F, 0.0F,
                                        kPlanetDefaultLocalDetailOuterHalfExtentM};
    cubey::math::Vec4 local_up_height{0.0F, 1.0F, 0.0F, kPlanetDefaultLocalDetailHeightStrengthM};
    cubey::math::Vec4 local_forward_scale{0.0F, 0.0F, 1.0F, kPlanetDefaultLocalDetailScaleM};
    cubey::math::Vec4 local_detail_options{static_cast<float>(kPlanetDefaultLocalDetailLodLevels),
                                           0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(PlanetSurfaceFrameUniforms) == sizeof(float) * 4U * 30U);

[[nodiscard]] std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PLANET_SHADER_DIR) / filename;
}

[[nodiscard]] cubey::render::CloudLayerRuntimeShaderFiles cloud_runtime_shader_files() {
    return cubey::render::cloud_layer_runtime_shader_files(
        std::filesystem::path(CUBEY_PLANET_SHADER_DIR),
        cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
}

[[nodiscard]] cubey::CloudEnvironmentConfig planet_cloud_config_from_run_config(
    const RunConfig& config) {
    cubey::CloudEnvironmentConfig clouds{};
    clouds.enabled = false;
    clouds.layer.quality = cubey::render::CloudLayerQuality::Full;
    clouds.layer.distance_mode = cubey::render::CloudLayerDistanceMode::Local;
    clouds.layer.density_model = cubey::render::CloudLayerDensityModel::SurfaceVolume;
    clouds.layer.orbit_representation = cubey::render::CloudLayerOrbitRepresentation::SurfaceShell;
    clouds.layer.temporal_enabled = false;
    clouds.layer.orbit_transition_start_m = 80000.0F;
    clouds.layer.orbit_transition_end_m = 260000.0F;
    clouds.layer.far_shell_start_m = 45000.0F;
    clouds.layer.far_shell_end_m = 220000.0F;
    clouds.layer.far_shell_strength = 1.10F;
    clouds.layer.orbit_detail_strength = 0.78F;
    clouds.layer.orbit_density_scale = 0.023F;
    clouds.layer.orbit_fill = 1.12F;
    cubey::apply_cloud_environment_run_config(
        clouds, config.clouds, cubey::CloudEnvironmentConfigPolicy::AllowDeferredDiagnostics);
    return clouds;
}

[[nodiscard]] cubey::render::MaterialPassInfo planet_pass_info() {
    return cubey::render::MaterialPassInfo{
        .label = "planet.forward",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = kPlanetSurfaceFrameUniformBinding,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] cubey::render::MaterialPassInfo planet_local_detail_pass_info() {
    cubey::render::MaterialPassInfo pass = planet_pass_info();
    pass.label = "planet.local_detail";
    pass.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
    return pass;
}

[[nodiscard]] cubey::render::VertexInputLayout planet_surface_vertex_input_layout() {
    cubey::render::VertexInputLayout layout;
    layout.vertex_bindings.push_back(cubey::render::vertex_input_binding(
        0, static_cast<std::uint32_t>(sizeof(PlanetPatchGridVertex)), VK_VERTEX_INPUT_RATE_VERTEX));
    layout.vertex_bindings.push_back(
        cubey::render::instance_input_binding<PlanetSurfaceGpuPatchInstance>(1));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PlanetPatchGridVertex, uv)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        1, 0, VK_FORMAT_R32_SFLOAT, offsetof(PlanetPatchGridVertex, skirt)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        2, 1, VK_FORMAT_R32G32B32A32_UINT, offsetof(PlanetSurfaceGpuPatchInstance, face)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        3, 1, VK_FORMAT_R32_SFLOAT, offsetof(PlanetSurfaceGpuPatchInstance, screen_error_px)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        4, 1, VK_FORMAT_R32_UINT, offsetof(PlanetSurfaceGpuPatchInstance, edge_transition_mask)));
    return layout;
}

[[nodiscard]] cubey::render::VertexInputLayout planet_local_detail_vertex_input_layout() {
    cubey::render::VertexInputLayout layout;
    layout.vertex_bindings.push_back(cubey::render::vertex_input_binding(
        0, static_cast<std::uint32_t>(sizeof(PlanetLocalDetailVertex)),
        VK_VERTEX_INPUT_RATE_VERTEX));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PlanetLocalDetailVertex, local_xz_m)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PlanetLocalDetailVertex, patch_uv)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        2, 0, VK_FORMAT_R32_SFLOAT, offsetof(PlanetLocalDetailVertex, level)));
    layout.attributes.push_back(cubey::render::vertex_input_attribute(
        3, 0, VK_FORMAT_R32_SFLOAT, offsetof(PlanetLocalDetailVertex, blend)));
    return layout;
}

[[nodiscard]] float planet_app_smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] float degrees_to_radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0F;
}

[[nodiscard]] cubey::math::Vec3 planet_cloud_normalize_or_up(cubey::math::Vec3 value) {
    if (glm::dot(value, value) <= 0.0000001F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(value);
}

[[nodiscard]] cubey::math::Vec3
planet_cloud_local_direction(cubey::math::Vec3 direction,
                             const cubey::render::LocalTangentFrame& frame) {
    const cubey::math::Vec3 normalized = planet_cloud_normalize_or_up(direction);
    return {
        glm::dot(normalized, frame.right),
        glm::dot(normalized, frame.up),
        glm::dot(normalized, frame.forward),
    };
}

[[nodiscard]] cubey::math::Vec3 planet_cloud_fixed_position(cubey::math::DVec3 world_position_m,
                                                            float planet_radius_m) {
    return {
        static_cast<float>(world_position_m.x),
        static_cast<float>(world_position_m.y - static_cast<double>(planet_radius_m)),
        static_cast<float>(world_position_m.z),
    };
}

[[nodiscard]] float packed_debug_wire_option(const PlanetConfig& config) {
    return static_cast<float>(static_cast<int>(config.debug_view)) +
           (config.wire_overlay ? 0.25F : 0.0F);
}

[[nodiscard]] float packed_patch_lod_option(const PlanetConfig& config) {
    return static_cast<float>(config.patch_resolution * 256U + config.patches_per_face * 16U +
                              config.max_lod_level);
}

[[nodiscard]] float packed_terrain_detail_strengths(const PlanetConfig& config) {
    constexpr float kQuantizeScale = 1024.0F;
    constexpr float kQuantizeBase = 4096.0F;
    const float mid = std::clamp(std::round(config.terrain_mid_detail_strength * kQuantizeScale),
                                 0.0F, kQuantizeBase - 1.0F);
    const float fine = std::clamp(std::round(config.terrain_fine_detail_strength * kQuantizeScale),
                                  0.0F, kQuantizeBase - 1.0F);
    return mid * kQuantizeBase + fine;
}

[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
planet_atmosphere_look_config_from_run_config(const RunConfig& config) {
    cubey::render::AtmosphereEnvironmentConfig look_config;
    cubey::apply_atmosphere_environment_look_options(look_config, config.atmosphere);
    return look_config;
}

class PlanetApp {
  public:
    explicit PlanetApp(RunConfig config)
        : config_(std::move(config)), planet_config_(planet_config_from_run_config(config_)),
          edit_planet_config_(planet_config_),
          solar_time_(planet_solar_time_from_run_config(config_)),
          exposure_config_(planet_exposure_config_from_run_config(config_)),
          atmosphere_look_config_(planet_atmosphere_look_config_from_run_config(config_)),
          clouds_config_(planet_cloud_config_from_run_config(config_)),
          celestial_system_(planet_celestial_system_from_solar_time(solar_time_)),
          celestial_lighting_(planet_celestial_lighting(celestial_system_)),
          camera_state_(planet_camera_initial_state_from_run_config(
              planet_config_, config_, kPlanetCameraBaseYaw, kPlanetCameraBasePitch)) {
        if (config_.headless) {
            apply_headless_initial_camera();
        }
        refresh_frame();
        surface_runtime_.rebuild(planet_config_, frame_, surface_view(default_surface_extent()));
        if (local_detail_surface_requested()) {
            local_detail_runtime_.rebuild(planet_config_, frame_,
                                          local_detail_view(default_surface_extent()));
        }
    }

    int run() {
        if (config_.headless) {
            return run_headless();
        }
        return run_windowed();
    }

  private:
    struct PlanetFrameGraph {
        cubey::render::CompiledRenderGraph graph{};
        cubey::render::RenderGraphTextureHandle post_scene_color{};
        cubey::render::RenderGraphTextureHandle scene_color{};
        cubey::render::RenderGraphTextureHandle depth{};
        cubey::render::RenderGraphTextureHandle cloud_scene_color{};
        cubey::render::CloudLayerRuntimeFrame cloud{};
        bool clouds_enabled = false;
    };

    int run_windowed() {
        cubey::host::WindowedAppCallbacks callbacks;
        callbacks.create_global_resources = [this](cubey::host::WindowedAppContext& context) {
            create_global_resources_if_needed(context.device(), context.gpu(),
                                              context.frame_slot_count());
        };
        callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
            create_forward_pass(context.device(), context.swapchain().extent(),
                                context.swapchain().format(), context.frame_slot_count());
        };
        callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
            destroy_swapchain_resources();
        };
        callbacks.update = [this](cubey::host::WindowedAppContext& context,
                                  const FrameTiming& timing) { update_input(context, timing); };
        callbacks.draw_ui = [this](cubey::host::WindowedAppContext& context) { draw_ui(context); };
        callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                        const cubey::host::WindowedRenderFrame& frame) {
            record_planet_frame(context.device(), context.gpu(), frame.command_buffer,
                                frame.color_target, frame.frame_slot, true);
        };
        callbacks.frame_stats_sample =
            [this](cubey::host::WindowedAppContext& context,
                   const FrameTiming& timing) -> std::optional<cubey::host::FrameStatsSample> {
            return record_frame_stats(context.swapchain().extent(), timing);
        };
        callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

        return cubey::host::run_windowed_app(
            {
                .run_config = config_,
                .app_name = kAppName,
                .ready_status = kReadyStatus,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
                .close_on_escape = true,
            },
            std::move(callbacks));
    }

    int run_headless() {
        cubey::host::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
        host_config.require_dynamic_rendering = true;

        cubey::host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
            create_global_resources_if_needed(
                context.device(), context.gpu(),
                cubey::host::headless_capture_frame_slot_count(config_));
            create_forward_pass(context.device(), context.render_target().extent,
                                context.render_target().format,
                                cubey::host::headless_capture_frame_slot_count(config_));
        };
        callbacks.before_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame) {
            if (frame.index > 0U) {
                update_solar_time(frame.timing.delta_seconds);
                update_cloud_time(frame.timing.delta_seconds);
                update_headless_capture_camera(frame.timing.delta_seconds,
                                               context.render_target().extent);
            }
        };
        callbacks.record_frame = [this](cubey::host::HeadlessPngContext& context,
                                        const cubey::host::HeadlessCaptureFrame& frame,
                                        VkCommandBuffer command_buffer,
                                        const cubey::host::HeadlessRenderTarget& target) {
            record_planet_frame(context.device(), context.gpu(), command_buffer, target,
                                frame.frame_slot, false);
        };
        callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

        cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
    }

    void create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu,
                                           std::uint32_t frame_slot_count) {
        if (!patch_grid_mesh_.has_value()) {
            patch_grid_mesh_.emplace(gpu, surface_runtime_.patch_grid().mesh_config());
        }
        if (!local_detail_mesh_.has_value()) {
            create_local_detail_mesh_if_needed(gpu);
        }
        if (!moon_mesh_.has_value()) {
            const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv>
                moon_mesh = cubey::render::make_uv_sphere_position_color_normal_uv_mesh({
                    .radius = 1.0F,
                    .latitude_segments = 32,
                    .longitude_segments = 64,
                    .color = {cubey::render::kCelestialMoonSurfaceColor.r,
                              cubey::render::kCelestialMoonSurfaceColor.g,
                              cubey::render::kCelestialMoonSurfaceColor.b},
                });
            moon_mesh_.emplace(gpu, moon_mesh.mesh_config());
        }
        if (!sky_atlas_resources_.has_value()) {
            sky_atlas_resources_.emplace(
                cubey::render::create_atmosphere_background_generated_textures(device, gpu));
        }
        if (clouds_config_.enabled) {
            create_cloud_resources(device, gpu);
        }
        if (!surface_frame_material_.has_value() ||
            surface_frame_material_->material().set_count() != frame_slot_count) {
            surface_frame_material_.reset();
            surface_frame_material_.emplace(
                device, cubey::render::FrameUniformMaterialInstanceConfig{
                            .material_pass = planet_pass_info(),
                            .descriptor_set = 0,
                            .frame_slot_count = frame_slot_count,
                            .uniform_binding = kPlanetSurfaceFrameUniformBinding,
                        });
        }
        surface_runtime_.resize_frame_slots(frame_slot_count);
        create_unified_atmosphere_frame_resources_if_needed(device, frame_slot_count);
        create_celestial_body_frame_resources_if_needed(device, frame_slot_count);
        create_hdr_post_resources_if_needed(device, frame_slot_count);
    }

    void create_forward_pass(const cubey::vulkan::Device& device, VkExtent2D extent,
                             VkFormat color_format, std::uint32_t frame_slot_count) {
        const std::array<cubey::render::ShaderStageFile, 2> shader_stage_files{
            cubey::render::vertex_shader_file(shader_path("planet_surface.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("planet_surface.frag.spv")),
        };
        const cubey::render::VertexInputLayout vertex_input = planet_surface_vertex_input_layout();
        const std::array descriptor_set_layouts{surface_frame_material().layout()};
        forward_pass_.emplace(
            device,
            cubey::render::GraphicsPipelineTargetInfo{
                .extent = extent,
                .color_format = kPlanetSceneColorFormat,
            },
            cubey::render::ForwardScenePass3DConfig{
                .pipeline =
                    {
                        .shader_stage_files = shader_stage_files,
                        .vertex_bindings = vertex_input.bindings(),
                        .vertex_attributes = vertex_input.attribute_descriptions(),
                        .descriptor_set_layouts = descriptor_set_layouts,
                        .material_pass = planet_pass_info(),
                    },
                .clear =
                    {
                        .color = cubey::render::color_clear_value(0.018F, 0.030F, 0.052F, 1.0F),
                        .depth = cubey::render::depth_clear_value(),
                    },
                .sampled_depth = true,
            });
        const std::array<cubey::render::ShaderStageFile, 2> local_detail_shaders{
            cubey::render::vertex_shader_file(shader_path("planet_local_detail.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("planet_surface.frag.spv")),
        };
        const cubey::render::VertexInputLayout local_detail_vertex_input =
            planet_local_detail_vertex_input_layout();
        local_detail_pipeline_.emplace(
            device, cubey::render::GraphicsPipelineFileResourceConfig{
                        .extent = extent,
                        .color_format = kPlanetSceneColorFormat,
                        .depth_format = forward_pass().depth_target().format,
                        .shader_stage_files = local_detail_shaders,
                        .vertex_bindings = local_detail_vertex_input.bindings(),
                        .vertex_attributes = local_detail_vertex_input.attribute_descriptions(),
                        .descriptor_set_layouts = descriptor_set_layouts,
                        .material_pass = planet_local_detail_pass_info(),
                    });
        create_unified_atmosphere_frame_pipeline(device, extent, kPlanetSceneColorFormat);
        if (cloud_global_resources_created_) {
            create_cloud_pipelines(device, extent, frame_slot_count);
        }
        create_celestial_body_frame_pipeline(device, extent, kPlanetSceneColorFormat,
                                             forward_pass().depth_target().format);
        create_hdr_post_pipeline(device, extent, color_format);
        graph_executor_.clear();
        graph_executor_.resize(frame_slot_count);
    }

    void destroy_swapchain_resources() {
        graph_executor_.clear();
        cloud_runtime_.destroy_swapchain_resources();
        hdr_post_frame_.destroy_pipeline();
        celestial_body_frame_.destroy_pipeline();
        unified_atmosphere_frame_.destroy_pipeline();
        local_detail_pipeline_.reset();
        forward_pass_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        hdr_post_frame_.destroy();
        hdr_post_frame_slot_count_ = 0;
        cloud_runtime_.destroy_generated_resources();
        cloud_global_resources_created_ = false;
        celestial_body_frame_.destroy();
        unified_atmosphere_frame_.destroy();
        surface_runtime_.clear_gpu_buffers();
        moon_mesh_.reset();
        sky_atlas_resources_.reset();
        local_detail_mesh_.reset();
        patch_grid_mesh_.reset();
        surface_frame_material_.reset();
    }

    void update_input(cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        update_solar_time(timing.delta_seconds);
        update_cloud_time(timing.delta_seconds);
        update_camera_input(context.filtered_input(), timing.delta_seconds);
        refresh_frame();
        if (patch_grid_mesh_.has_value() &&
            surface_runtime_.plan_changed(planet_config_, frame_,
                                          surface_view(context.swapchain().extent()))) {
            surface_rebuild_pending_ = true;
        }
        if (patch_grid_mesh_.has_value() && surface_rebuild_pending_ && !camera_interacting_) {
            rebuild_surface_resources(context.swapchain().extent());
            surface_rebuild_pending_ = false;
        }
        if (local_detail_surface_requested() && !camera_interacting_ &&
            local_detail_runtime_.topology_changed(
                planet_config_, local_detail_view(context.swapchain().extent()))) {
            rebuild_local_detail_resources(context, context.swapchain().extent());
        }
        if (!local_detail_surface_requested()) {
            local_detail_mesh_.reset();
        } else {
            create_local_detail_mesh_if_needed(context.gpu());
        }
    }

    void update_camera_input(const cubey::input::FilteredInputFrame& input, double delta_seconds) {
        camera_interacting_ = false;
        if (input.key_pressed(cubey::input::Key::R)) {
            reset_camera();
            camera_interacting_ = true;
        }

        const double scroll_y = input.scroll_delta().y;
        if (scroll_y != 0.0) {
            planet_camera_zoom_by_scroll(camera_state_, planet_config_, scroll_y);
            camera_interacting_ = true;
        }

        const float surface_blend = planet_surface_camera_blend_from_clearance(
            planet_config_,
            planet_camera_surface_clearance_m(planet_config_, camera_state_.position_m));
        if (input.mouse_button_down(cubey::input::MouseButton::Right) && surface_blend >= 0.20F) {
            const cubey::input::PointerDelta delta =
                input.mouse_button_delta(cubey::input::MouseButton::Right);
            planet_camera_surface_look_drag(camera_state_, planet_config_, delta.x, delta.y);
            camera_interacting_ = true;
        } else if (input.mouse_button_down(cubey::input::MouseButton::Left) &&
                   surface_blend < 0.85F) {
            const cubey::input::PointerDelta delta =
                input.mouse_button_delta(cubey::input::MouseButton::Left);
            planet_camera_orbit_drag(camera_state_, planet_config_, delta.x, delta.y);
            camera_interacting_ = true;
        }

        float forward = 0.0F;
        float right = 0.0F;
        if (input.key_down(cubey::input::Key::W)) {
            forward += 1.0F;
        }
        if (input.key_down(cubey::input::Key::S)) {
            forward -= 1.0F;
        }
        if (input.key_down(cubey::input::Key::D)) {
            right += 1.0F;
        }
        if (input.key_down(cubey::input::Key::A)) {
            right -= 1.0F;
        }
        camera_interacting_ = planet_camera_surface_move(camera_state_, planet_config_, forward,
                                                         right, delta_seconds) ||
                              camera_interacting_;
        planet_camera_update_surface_mode(camera_state_, planet_config_);
    }

    void draw_ui(cubey::host::WindowedAppContext& context) {
        const VkExtent2D ui_extent = context.swapchain().extent();
        cubey::host::PerformanceUiContext performance{
            .frame_stats = latest_frame_stats_,
            .latest_fps = latest_fps_,
            .latest_frame_ms = latest_frame_ms_,
            .process = process_stats_.sample(),
            .device_memory_budget = context.device().device_memory_budget(),
            .config = {.default_open = false},
        };

        draw_planet_ui(PlanetUiContext{
            .edit_config = edit_planet_config_,
            .active_config = planet_config_,
            .config_apply_pending = planet_config_apply_pending_,
            .rebuild_error = rebuild_error_,
            .solar_time = solar_time_,
            .atmosphere_look_config = atmosphere_look_config_,
            .clouds_config = clouds_config_,
            .solar_config = solar_config_,
            .celestial_system = celestial_system_,
            .celestial_lighting = celestial_lighting_,
            .frame = frame_,
            .camera_state = camera_state_,
            .exposure_config = exposure_config_,
            .surface_diagnostics = surface_runtime_.diagnostics(),
            .local_detail_diagnostics = local_detail_runtime_.diagnostics(),
            .local_detail_surface_weight = local_detail_surface_weight(),
            .cloud_view_regime = planet_cloud_view_regime(ui_extent),
            .cloud_scene_depth_occlusion_enabled = cloud_layer_enabled(),
            .cloud_scene_depth_fade_m = kPlanetCloudSceneDepthFadeM,
            .performance = performance,
            .extent = ui_extent,
            .reset_camera = [this]() { reset_camera(); },
            .maybe_apply_config = [this, &context]() { maybe_apply_planet_config(context); },
            .refresh_celestial_state = [this]() { refresh_celestial_state(); },
            .view_light_fraction =
                [this](VkExtent2D extent) { return view_light_fraction(extent); },
            .display_exposure = [this](VkExtent2D extent) { return display_exposure(extent); },
        });
        sync_cloud_runtime_after_ui(context);
    }

    [[nodiscard]] std::optional<cubey::host::FrameStatsSample>
    record_frame_stats(VkExtent2D extent, const FrameTiming& timing) {
        latest_frame_ms_ = timing.delta_seconds * 1000.0;
        latest_fps_ = timing.delta_seconds > 0.0 ? 1.0 / timing.delta_seconds : 0.0;

        const cubey::host::FrameStatsSample sample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles =
                surface_runtime_.diagnostics().triangle_count +
                (local_detail_draw_enabled() ? local_detail_runtime_.diagnostics().triangle_count
                                             : 0U),
        };
        if (std::optional<cubey::host::FrameStatsSnapshot> stats =
                ui_frame_stats_.record_frame(sample);
            stats.has_value()) {
            latest_frame_stats_ = stats.value();
        }
        return sample;
    }

    void rebuild_planet_resources(cubey::host::WindowedAppContext& context) {
        validate_planet_config(edit_planet_config_);
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle planet topology rebuild");
        static_cast<void>(context.gpu().drain());
        surface_runtime_.clear_gpu_buffers();
        patch_grid_mesh_.reset();
        local_detail_mesh_.reset();
        surface_runtime_.clear_selection_history();

        planet_config_ = edit_planet_config_;
        refresh_camera_limits_for_planet();
        refresh_frame();
        surface_runtime_.rebuild(planet_config_, frame_,
                                 surface_view(context.swapchain().extent()));
        patch_grid_mesh_.emplace(context.gpu(), surface_runtime_.patch_grid().mesh_config());
        if (local_detail_surface_requested()) {
            local_detail_runtime_.rebuild(planet_config_, frame_,
                                          local_detail_view(context.swapchain().extent()));
        } else {
            local_detail_runtime_.clear();
        }
        create_local_detail_mesh_if_needed(context.gpu());
        surface_runtime_.resize_frame_slots(context.frame_slot_count());
        static_cast<void>(context.gpu().drain());
    }

    void apply_dynamic_planet_config(VkExtent2D extent) {
        validate_planet_config(edit_planet_config_);
        planet_config_ = edit_planet_config_;
        refresh_camera_limits_for_planet();
        refresh_frame();
        surface_runtime_.rebuild(planet_config_, frame_, surface_view(extent));
        if (!local_detail_surface_requested()) {
            local_detail_runtime_.clear();
            local_detail_mesh_.reset();
        }
    }

    void maybe_apply_planet_config(cubey::host::WindowedAppContext& context) {
        if (!planet_config_apply_pending_ || ImGui::IsAnyItemActive()) {
            return;
        }
        const PlanetConfigChangeKind change_kind =
            planet_config_change_kind(planet_config_, edit_planet_config_);
        if (change_kind == PlanetConfigChangeKind::None) {
            planet_config_apply_pending_ = false;
            return;
        }
        try {
            if (change_kind == PlanetConfigChangeKind::SurfaceTopology ||
                change_kind == PlanetConfigChangeKind::LocalDetailTopology) {
                rebuild_planet_resources(context);
            } else {
                apply_dynamic_planet_config(context.swapchain().extent());
            }
            rebuild_error_.clear();
            planet_config_apply_pending_ = false;
        } catch (const std::exception& error) {
            rebuild_error_ = error.what();
            planet_config_apply_pending_ = false;
        } catch (...) {
            rebuild_error_ = "unknown planet rebuild error";
            planet_config_apply_pending_ = false;
        }
    }

    void rebuild_surface_resources(VkExtent2D extent) {
        surface_runtime_.rebuild(planet_config_, frame_, surface_view(extent));
    }

    void rebuild_local_detail_resources(cubey::host::WindowedAppContext& context,
                                        VkExtent2D extent) {
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle planet local-detail rebuild");
        static_cast<void>(context.gpu().drain());
        local_detail_mesh_.reset();
        local_detail_runtime_.rebuild(planet_config_, frame_, local_detail_view(extent));
        create_local_detail_mesh_if_needed(context.gpu());
        static_cast<void>(context.gpu().drain());
    }

    void refresh_camera_limits_for_planet() {
        planet_camera_set_distance(camera_state_, planet_config_,
                                   planet_camera_distance_m(camera_state_));
        planet_camera_update_surface_mode(camera_state_, planet_config_);
    }

    void reset_camera() {
        camera_state_ =
            planet_camera_home_state(planet_config_, kPlanetCameraBaseYaw, kPlanetCameraBasePitch);
    }

    void apply_headless_initial_camera() {
        if (config_.planet.camera_mode != "surface") {
            return;
        }
        if (config_.planet.camera_surface_look == "sun") {
            planet_camera_surface_look_at_direction(camera_state_, planet_config_,
                                                    celestial_system_.sun.direction);
        } else if (config_.planet.camera_surface_look == "antisun") {
            planet_camera_surface_look_at_direction(camera_state_, planet_config_,
                                                    -celestial_system_.sun.direction);
        }
        if (run_config_float_is_set(config_.planet.camera_surface_yaw_degrees)) {
            planet_camera_surface_look_rotate(
                camera_state_, planet_config_,
                degrees_to_radians(config_.planet.camera_surface_yaw_degrees), 0.0F);
        }
        if (run_config_float_is_set(config_.planet.camera_surface_pitch_degrees)) {
            planet_camera_surface_look_rotate(
                camera_state_, planet_config_, 0.0F,
                degrees_to_radians(config_.planet.camera_surface_pitch_degrees));
        }
    }

    void update_headless_capture_camera(double delta_seconds, VkExtent2D extent) {
        if (delta_seconds <= 0.0 ||
            !run_config_float_is_set(config_.planet.camera_orbit_spin_degrees_per_second) ||
            config_.planet.camera_mode == "surface") {
            return;
        }

        planet_camera_orbit_rotate(
            camera_state_, planet_config_,
            degrees_to_radians(config_.planet.camera_orbit_spin_degrees_per_second) *
                static_cast<float>(delta_seconds),
            0.0F);
        refresh_frame();
        const PlanetSurfaceView view = surface_view(extent);
        if (surface_runtime_.plan_changed(planet_config_, frame_, view)) {
            surface_runtime_.rebuild(planet_config_, frame_, view);
        }
    }

    [[nodiscard]] cubey::Transform3D camera_transform() const {
        return make_planet_camera_transform(planet_config_, camera_state_);
    }

    void refresh_frame() {
        frame_ = make_planet_frame(planet_config_, camera_state_.position_m);
        camera_.set_projection(std::numbers::pi_v<float> / 3.0F, frame_.near_plane_m,
                               frame_.far_plane_m);
    }

    [[nodiscard]] static VkExtent2D default_surface_extent() {
        return {1280U, 720U};
    }

    [[nodiscard]] PlanetSurfaceView surface_view(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const float surface_blend = planet_surface_camera_blend_from_clearance(
            planet_config_, frame_.camera_surface_clearance_m);
        const float surface_target =
            std::max(planet_config_.lod_target_edge_px, kPlanetSurfaceBaseLodTargetEdgePx);
        return {
            .camera_world_position_m = frame_.camera_world_position_m,
            .camera_forward_world =
                glm::normalize(transform.rotation * cubey::math::Vec3{0.0F, 0.0F, -1.0F}),
            .vertical_fov_radians = camera_.fovy_radians(),
            .aspect_ratio = aspect,
            .viewport_height_px = static_cast<float>(std::max(extent.height, 1U)),
            .lod_target_edge_px =
                std::lerp(planet_config_.lod_target_edge_px, surface_target, surface_blend),
            .culling_enabled = true,
        };
    }

    [[nodiscard]] PlanetLocalDetailView local_detail_view(VkExtent2D extent) const {
        const bool horizon_inspection =
            planet_debug_view_uses_horizon_local_detail(planet_config_.debug_view);
        const float inspection_outer_half_extent =
            horizon_inspection ? std::max({planet_config_.local_detail_outer_half_extent_m,
                                           kPlanetLocalDetailInspectionMinimumOuterHalfExtentM,
                                           frame_.horizon_distance_m *
                                               kPlanetLocalDetailInspectionHorizonExtentScale})
                               : 0.0F;
        return {
            .camera_clearance_m = std::max(frame_.camera_surface_clearance_m, 1.0F),
            .vertical_fov_radians = camera_.fovy_radians(),
            .viewport_height_px = static_cast<float>(std::max(extent.height, 1U)),
            .full_active_range = horizon_inspection,
            .minimum_lod_levels = horizon_inspection ? kPlanetMaxLocalDetailLodLevels : 0U,
            .minimum_outer_half_extent_m = inspection_outer_half_extent,
        };
    }

    [[nodiscard]] cubey::Transform3D camera_render_transform() const {
        cubey::Transform3D transform = camera_transform();
        const cubey::math::DVec3 relative_camera =
            frame_.camera_world_position_m - surface_runtime_.render_origin_world_m();
        transform.translation = {
            static_cast<float>(relative_camera.x),
            static_cast<float>(relative_camera.y),
            static_cast<float>(relative_camera.z),
        };
        return transform;
    }

    [[nodiscard]] PlanetSurfaceFrameUniforms surface_frame_uniforms(VkExtent2D extent) {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        refresh_frame();
        const cubey::Transform3D transform = camera_render_transform();
        const float terrain_height =
            planet_config_.terrain_enabled ? planet_config_.terrain_height_scale_m : 0.0F;
        const float skirt_depth = std::max(surface_runtime_.diagnostics().min_skirt_depth_m,
                                           planet_config_.radius_m * 0.00001F);
        const float ambient_intensity = surface_ambient_intensity();
        const float direct_intensity = surface_direct_intensity();
        const cubey::math::Vec3 haze_color = surface_haze_color();
        const cubey::render::ClipmapGrid2DConfig local_detail_grid =
            planet_local_detail_clipmap_config(planet_config_);
        const PlanetLocalDetailDiagnostics& local_detail_diagnostics =
            local_detail_runtime_.diagnostics();
        const float local_detail_active = local_detail_surface_weight();
        const float local_detail_outer_half_extent =
            local_detail_diagnostics.active ? local_detail_diagnostics.active_outer_half_extent
                                            : planet_config_.local_detail_outer_half_extent_m;
        const float local_detail_lod_levels =
            local_detail_diagnostics.active
                ? static_cast<float>(local_detail_diagnostics.active_last_level + 1U)
                : static_cast<float>(planet_config_.local_detail_lod_levels);
        const float local_detail_near_half_extent =
            cubey::render::clipmap_grid_2d_near_half_extent(local_detail_grid);
        const float local_detail_near_cell_size =
            local_detail_diagnostics.active
                ? local_detail_diagnostics.finest_active_cell_size
                : cubey::render::clipmap_grid_2d_near_cell_size(local_detail_grid);
        const PlanetCelestialDiagnostics celestial_diagnostics =
            planet_celestial_diagnostics(solar_time_, solar_config_);
        const PlanetAtmosphereInputs atmosphere_inputs = planet_atmosphere_inputs(
            celestial_system_, celestial_lighting_, frame_.camera_world_position_m,
            planet_config_.radius_m, planet_config_.radius_m + planet_config_.atmosphere_height_m);
        const cubey::render::AtmosphereEnvironmentConfig atmosphere_config =
            planet_atmosphere_environment_config(atmosphere_inputs, atmosphere_look_config_);
        return {
            .view_projection = camera_.view_projection_matrix(transform, aspect),
            .light_direction_debug =
                {
                    celestial_lighting_.primary_light_direction.x,
                    celestial_lighting_.primary_light_direction.y,
                    celestial_lighting_.primary_light_direction.z,
                    planet_config_.lod_target_edge_px,
                },
            .render_origin_radius =
                {
                    static_cast<float>(surface_runtime_.render_origin_world_m().x),
                    static_cast<float>(surface_runtime_.render_origin_world_m().y),
                    static_cast<float>(surface_runtime_.render_origin_world_m().z),
                    planet_config_.radius_m,
                },
            .surface_options =
                {
                    packed_debug_wire_option(planet_config_),
                    packed_patch_lod_option(planet_config_),
                    packed_terrain_detail_strengths(planet_config_),
                    planet_config_.terrain_fine_detail_scale,
                },
            .terrain_options =
                {
                    terrain_height,
                    planet_config_.terrain_noise_scale,
                    static_cast<float>(planet_config_.terrain_seed),
                    skirt_depth,
                },
            .field_options =
                {
                    planet_config_.sea_level_m,
                    planet_config_.bathymetry_depth_scale_m,
                    planet_config_.shoreline_width_m,
                    0.0F,
                },
            .camera_horizon =
                {
                    transform.translation.x,
                    transform.translation.y,
                    transform.translation.z,
                    frame_.horizon_distance_m,
                },
            .atmosphere_options =
                {
                    ambient_intensity,
                    planet_config_.atmosphere_haze_strength,
                    planet_config_.atmosphere_haze_start,
                    planet_config_.atmosphere_haze_end,
                },
            .haze_color_direct =
                {
                    haze_color.r,
                    haze_color.g,
                    haze_color.b,
                    direct_intensity,
                },
            .celestial_equator_plane =
                {
                    celestial_diagnostics.equator_plane_normal.x,
                    celestial_diagnostics.equator_plane_normal.y,
                    celestial_diagnostics.equator_plane_normal.z,
                    0.0F,
                },
            .celestial_ecliptic_plane =
                {
                    celestial_diagnostics.ecliptic_plane_normal.x,
                    celestial_diagnostics.ecliptic_plane_normal.y,
                    celestial_diagnostics.ecliptic_plane_normal.z,
                    0.0F,
                },
            .celestial_moon_orbit_plane =
                {
                    celestial_diagnostics.moon_orbit_plane_normal.x,
                    celestial_diagnostics.moon_orbit_plane_normal.y,
                    celestial_diagnostics.moon_orbit_plane_normal.z,
                    0.0F,
                },
            .celestial_sun_direction =
                {
                    celestial_diagnostics.sun_direction.x,
                    celestial_diagnostics.sun_direction.y,
                    celestial_diagnostics.sun_direction.z,
                    0.0F,
                },
            .celestial_moon_direction =
                {
                    celestial_diagnostics.moon_direction.x,
                    celestial_diagnostics.moon_direction.y,
                    celestial_diagnostics.moon_direction.z,
                    celestial_diagnostics.moon_phase_fraction,
                },
            .camera_world_radius =
                {
                    static_cast<float>(frame_.camera_world_position_m.x),
                    static_cast<float>(frame_.camera_world_position_m.y),
                    static_cast<float>(frame_.camera_world_position_m.z),
                    frame_.camera_radius_m,
                },
            .atmosphere_radius_mode =
                {
                    frame_.atmosphere_outer_radius_m,
                    planet_config_.atmosphere_aerial_strength,
                    static_cast<float>(static_cast<std::uint32_t>(planet_config_.atmosphere_mode)),
                    celestial_lighting_.primary_light_angular_radius_rad,
                },
            .atmosphere_rayleigh =
                {
                    atmosphere_config.rayleigh_scattering.x *
                        atmosphere_config.rayleigh_density_scale,
                    atmosphere_config.rayleigh_scattering.y *
                        atmosphere_config.rayleigh_density_scale,
                    atmosphere_config.rayleigh_scattering.z *
                        atmosphere_config.rayleigh_density_scale,
                    atmosphere_config.rayleigh_scale_height_km,
                },
            .atmosphere_mie =
                {
                    atmosphere_config.mie_scattering * atmosphere_config.mie_density_scale,
                    atmosphere_config.mie_extinction * atmosphere_config.mie_density_scale,
                    atmosphere_config.mie_scale_height_km,
                    atmosphere_config.mie_anisotropy,
                },
            .atmosphere_ozone =
                {
                    atmosphere_config.ozone_absorption.x * atmosphere_config.ozone_density_scale,
                    atmosphere_config.ozone_absorption.y * atmosphere_config.ozone_density_scale,
                    atmosphere_config.ozone_absorption.z * atmosphere_config.ozone_density_scale,
                    atmosphere_config.ozone_center_altitude_km,
                },
            .atmosphere_shared_options =
                {
                    atmosphere_config.ozone_half_width_km,
                    kPlanetSharedAtmosphereSunRadiance,
                    kPlanetSharedAtmosphereMinTwilightSoftness,
                    0.0F,
                },
            .sun_color_intensity =
                {
                    celestial_lighting_.primary_light_color.r,
                    celestial_lighting_.primary_light_color.g,
                    celestial_lighting_.primary_light_color.b,
                    celestial_lighting_.primary_light_intensity,
                },
            .moon_color_intensity =
                {
                    celestial_lighting_.moon_light_color.r,
                    celestial_lighting_.moon_light_color.g,
                    celestial_lighting_.moon_light_color.b,
                    celestial_lighting_.moon_light_intensity,
                },
            .local_origin_options =
                {
                    static_cast<float>(frame_.local_frame.world_origin_m.x),
                    static_cast<float>(frame_.local_frame.world_origin_m.y),
                    static_cast<float>(frame_.local_frame.world_origin_m.z),
                    local_detail_active,
                },
            .local_right_outer =
                {
                    frame_.local_frame.right.x,
                    frame_.local_frame.right.y,
                    frame_.local_frame.right.z,
                    local_detail_outer_half_extent,
                },
            .local_up_height =
                {
                    frame_.local_frame.up.x,
                    frame_.local_frame.up.y,
                    frame_.local_frame.up.z,
                    planet_config_.local_detail_height_strength_m,
                },
            .local_forward_scale =
                {
                    frame_.local_frame.forward.x,
                    frame_.local_frame.forward.y,
                    frame_.local_frame.forward.z,
                    planet_config_.local_detail_scale_m,
                },
            .local_detail_options =
                {
                    local_detail_lod_levels,
                    local_detail_near_half_extent,
                    local_detail_near_cell_size,
                    static_cast<float>(local_detail_diagnostics.active_first_level),
                },
        };
    }

    [[nodiscard]] float local_detail_surface_weight() const {
        if (!planet_config_.local_detail_enabled ||
            !planet_debug_view_uses_local_detail_surface(planet_config_.debug_view)) {
            return 0.0F;
        }
        const PlanetLocalDetailDiagnostics& diagnostics = local_detail_runtime_.diagnostics();
        if (!diagnostics.active) {
            return 0.0F;
        }
        return planet_app_smoothstep(0.20F, 0.65F, exposure_surface_reference_weight());
    }

    [[nodiscard]] bool local_detail_surface_requested() const {
        return planet_config_.local_detail_enabled &&
               planet_debug_view_uses_local_detail_surface(planet_config_.debug_view);
    }

    [[nodiscard]] bool local_detail_draw_enabled() const {
        return local_detail_surface_requested() && local_detail_mesh_.has_value() &&
               local_detail_runtime_.has_drawable_mesh() && local_detail_surface_weight() > 0.001F;
    }

    [[nodiscard]] cubey::render::CloudLayerConfig planet_cloud_config(float elapsed_seconds) const {
        cubey::render::CloudLayerConfig config = clouds_config_.layer;
        config.planet_radius_m = planet_config_.radius_m;
        config.background_mode = cubey::render::CloudLayerBackgroundMode::Atmosphere;
        config.wind_offset_m = elapsed_seconds * clouds_config_.wind_speed_mps;
        return config;
    }

    [[nodiscard]] bool cloud_layer_enabled() const noexcept {
        return planet_config_.debug_view == PlanetDebugView::Final && clouds_config_.enabled &&
               cloud_global_resources_created_;
    }

    void update_cloud_time(double delta_seconds) {
        if (delta_seconds > 0.0) {
            cloud_elapsed_seconds_ += static_cast<float>(delta_seconds);
        }
    }

    void create_cloud_resources(const cubey::vulkan::Device& device,
                                cubey::vulkan::GpuRuntime& gpu) {
        if (cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.create_generated_resources(device, gpu,
                                                  cloud_runtime_shader_files().generated,
                                                  planet_cloud_config(cloud_elapsed_seconds_));
        cloud_global_resources_created_ = true;
    }

    void refresh_cloud_weather_if_needed(const cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu) {
        if (!cloud_global_resources_created_) {
            return;
        }
        cloud_runtime_.update_weather_texture(device, gpu,
                                              cloud_runtime_shader_files().generated.weather,
                                              planet_cloud_config(cloud_elapsed_seconds_));
    }

    void sync_cloud_runtime_after_ui(cubey::host::WindowedAppContext& context) {
        if (!clouds_config_.enabled || cloud_global_resources_created_) {
            return;
        }
        cubey::vulkan::check(vkDeviceWaitIdle(context.device().handle()),
                             "vkDeviceWaitIdle planet cloud enable");
        static_cast<void>(context.gpu().drain());
        create_cloud_resources(context.device(), context.gpu());
        create_cloud_pipelines(context.device(), context.swapchain().extent(),
                               context.frame_slot_count());
        static_cast<void>(context.gpu().drain());
    }

    [[nodiscard]] cubey::render::CloudLayerViewRegime
    planet_cloud_view_regime(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const cubey::render::ViewRayBasis3D world_rays =
            cubey::render::view_ray_basis_3d(transform.rotation, aspect, camera_.fovy_radians());
        const cubey::render::LocalTangentFrame& tangent = frame_.local_frame;
        const cubey::math::Vec3 camera_position =
            cubey::render::local_tangent_world_to_local_m(tangent, frame_.camera_world_position_m);
        const cubey::math::Vec3 camera_forward =
            planet_cloud_local_direction(cubey::math::Vec3{world_rays.forward}, tangent);
        const cubey::render::CloudLayerConfig config = planet_cloud_config(cloud_elapsed_seconds_);

        return cubey::render::cloud_layer_view_regime({
            .camera_position = {camera_position.x, config.planet_radius_m + camera_position.y,
                                camera_position.z},
            .camera_forward = camera_forward,
            .planet_radius_m = config.planet_radius_m,
            .orbit_transition_start_m = config.orbit_transition_start_m,
            .orbit_transition_end_m = config.orbit_transition_end_m,
        });
    }

    [[nodiscard]] cubey::render::CloudLayerFrameUniforms
    cloud_frame_uniforms(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const cubey::render::ViewRayBasis3D world_rays =
            cubey::render::view_ray_basis_3d(transform.rotation, aspect, camera_.fovy_radians());
        const cubey::render::LocalTangentFrame& tangent = frame_.local_frame;
        const cubey::math::Vec3 local_camera_position =
            cubey::render::local_tangent_world_to_local_m(tangent, frame_.camera_world_position_m);
        const cubey::math::Vec3 local_camera_right =
            planet_cloud_local_direction(cubey::math::Vec3{world_rays.right_aspect}, tangent);
        const cubey::math::Vec3 local_camera_up =
            planet_cloud_local_direction(cubey::math::Vec3{world_rays.up_tan_half_fovy}, tangent);
        const cubey::math::Vec3 local_camera_forward =
            planet_cloud_local_direction(cubey::math::Vec3{world_rays.forward}, tangent);
        const cubey::math::Vec3 local_sun_direction =
            planet_cloud_local_direction(celestial_lighting_.primary_light_direction, tangent);
        const cubey::render::CloudLayerConfig config = planet_cloud_config(cloud_elapsed_seconds_);
        const cubey::render::CloudLayerViewRegime view_regime =
            cubey::render::cloud_layer_view_regime({
                .camera_position = {local_camera_position.x,
                                    config.planet_radius_m + local_camera_position.y,
                                    local_camera_position.z},
                .camera_forward = local_camera_forward,
                .planet_radius_m = config.planet_radius_m,
                .orbit_transition_start_m = config.orbit_transition_start_m,
                .orbit_transition_end_m = config.orbit_transition_end_m,
            });
        // Full-orbit cloud rendering samples the shell in a planet-fixed frame; the
        // surface/high regimes stay camera-relative to preserve local tangent precision.
        const bool fixed_orbit_frame = view_regime.camera_mode >= 3.5F;
        const cubey::math::Vec3 camera_position =
            fixed_orbit_frame ? planet_cloud_fixed_position(frame_.camera_world_position_m,
                                                            config.planet_radius_m)
                              : local_camera_position;
        const cubey::math::Vec3 camera_right =
            fixed_orbit_frame ? cubey::math::Vec3{world_rays.right_aspect} : local_camera_right;
        const cubey::math::Vec3 camera_up =
            fixed_orbit_frame ? cubey::math::Vec3{world_rays.up_tan_half_fovy} : local_camera_up;
        const cubey::math::Vec3 camera_forward =
            fixed_orbit_frame ? cubey::math::Vec3{world_rays.forward} : local_camera_forward;
        const cubey::math::Vec3 sun_direction =
            fixed_orbit_frame ? celestial_lighting_.primary_light_direction : local_sun_direction;

        return cubey::render::cloud_layer_frame_uniforms(
            config, cubey::render::CloudLayerFrameInfo{
                        .camera_position = camera_position,
                        .camera_right = camera_right,
                        .camera_up = camera_up,
                        .camera_forward = camera_forward,
                        .tan_half_fovy = world_rays.up_tan_half_fovy.w,
                        .sun_direction = sun_direction,
                        .sun_intensity = celestial_lighting_.primary_light_intensity,
                        .target_extent = extent,
                        .temporal_frame_index = cloud_runtime_.temporal_frame_index(),
                        .camera_mode = view_regime.camera_mode,
                        .external_background = true,
                        .near_plane_m = frame_.near_plane_m,
                        .far_plane_m = frame_.far_plane_m,
                        .scene_depth_mode = cubey::render::CloudLayerSceneDepthMode::DistanceAware,
                        .scene_depth_fade_m = kPlanetCloudSceneDepthFadeM,
                    });
    }

    [[nodiscard]] float surface_direct_intensity() const {
        return std::clamp(celestial_lighting_.primary_light_intensity, 0.03F, 1.20F);
    }

    [[nodiscard]] float surface_ambient_intensity() const {
        return std::clamp(celestial_lighting_.ambient_intensity, 0.025F, 0.30F);
    }

    [[nodiscard]] cubey::math::Vec3 surface_haze_color() const {
        return celestial_lighting_.haze_color;
    }

    void refresh_celestial_state() {
        celestial_system_ = planet_celestial_system_from_solar_time(solar_time_, solar_config_);
        celestial_lighting_ = planet_celestial_lighting(celestial_system_);
    }

    void update_solar_time(double delta_seconds) {
        const PlanetSolarTime before = solar_time_;
        planet_solar_time_advance(solar_time_, delta_seconds);
        if (solar_time_.day_of_year != before.day_of_year ||
            solar_time_.time_hours != before.time_hours) {
            refresh_celestial_state();
        }
    }

    [[nodiscard]] cubey::render::AtmosphereEnvironmentFrameUniforms
    unified_atmosphere_frame_uniforms(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const PlanetAtmosphereInputs inputs = planet_atmosphere_inputs(
            celestial_system_, celestial_lighting_, frame_.camera_world_position_m,
            planet_config_.radius_m, planet_config_.radius_m + planet_config_.atmosphere_height_m);
        return planet_unified_atmosphere_frame_uniforms(
            inputs, {
                        .view_rays = cubey::render::view_ray_basis_3d(transform.rotation, aspect,
                                                                      camera_.fovy_radians()),
                        .local_frame = frame_.local_frame,
                        .look_config = atmosphere_look_config_,
                    });
    }

    [[nodiscard]] PlanetCelestialBodyFrameUniforms
    moon_body_frame_uniforms(VkExtent2D extent) const {
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        const cubey::Transform3D transform = camera_render_transform();
        const PlanetCelestialBody moon = planet_celestial_moon_body(celestial_system_);
        const PlanetCelestialBodyRenderPlacement placement = planet_celestial_body_render_placement(
            moon, {
                      .camera_render_position_m = transform.translation,
                      .camera_world_position_m = frame_.camera_world_position_m,
                      .planet_center_world_position_m = {0.0, 0.0, 0.0},
                      .near_plane_m = frame_.near_plane_m,
                      .far_plane_m = frame_.far_plane_m,
                      .angular_radius_scale = kPlanetMoonAngularRadiusScale,
                      .shell_distance_fraction = kPlanetMoonShellDistanceFraction,
                  });
        return planet_celestial_body_frame_uniforms(
            moon, placement, celestial_lighting_, camera_.view_projection_matrix(transform, aspect),
            {
                .camera_render_position_m = transform.translation,
                .atmosphere =
                    {
                        .camera_position_m =
                            {
                                static_cast<float>(frame_.camera_world_position_m.x),
                                static_cast<float>(frame_.camera_world_position_m.y),
                                static_cast<float>(frame_.camera_world_position_m.z),
                            },
                        .planet_radius_m = planet_config_.radius_m,
                        .atmosphere_outer_radius_m =
                            planet_config_.radius_m + planet_config_.atmosphere_height_m,
                    },
            });
    }

    void record_moon_body_frame(const cubey::vulkan::CommandRecorder& recorder,
                                const cubey::render::RenderTargetView& target,
                                cubey::render::FrameSlot frame_slot) const {
        celestial_body_frame_.record_pass(recorder, target, frame_slot, moon_mesh());
    }

    [[nodiscard]] PlanetExposureView exposure_view(VkExtent2D extent) const {
        const cubey::Transform3D transform = camera_transform();
        const float aspect = extent.height == 0U ? 1.0F
                                                 : static_cast<float>(extent.width) /
                                                       static_cast<float>(extent.height);
        return {
            .view_rays = cubey::render::view_ray_basis_3d(transform.rotation, aspect,
                                                          camera_.fovy_radians()),
            .planet_radius_m = planet_config_.radius_m,
        };
    }

    [[nodiscard]] float view_light_fraction(VkExtent2D extent) const {
        return planet_celestial_view_light_fraction(
            celestial_system_, frame_.camera_world_position_m, exposure_view(extent));
    }

    [[nodiscard]] float display_exposure(VkExtent2D extent) const {
        return planet_celestial_display_exposure(
            celestial_system_, frame_.camera_world_position_m, exposure_config_,
            exposure_surface_reference_weight(), exposure_view(extent));
    }

    [[nodiscard]] float exposure_surface_reference_weight() const {
        return planet_surface_camera_blend_from_clearance(
            planet_config_,
            planet_camera_surface_clearance_m(planet_config_, camera_state_.position_m));
    }

    [[nodiscard]] cubey::render::PbrPostUniforms post_uniforms(VkFormat color_format,
                                                               VkExtent2D extent) const {
        const float exposure =
            planet_config_.debug_view == PlanetDebugView::Final ? display_exposure(extent) : 0.0F;
        return cubey::render::hdr_post_uniforms(color_format, exposure);
    }

    void record_post_pass(const cubey::vulkan::CommandRecorder& recorder,
                          cubey::render::ColorTargetView target,
                          cubey::render::FrameSlot frame_slot) const {
        hdr_post_frame_.record_pass(recorder, target, frame_slot);
    }

    template <typename RecordCallback>
    void record_planet_surface_pass(const cubey::vulkan::CommandRecorder& recorder,
                                    const cubey::render::RenderTargetView& target,
                                    RecordCallback&& record_callback) const {
        const cubey::render::RenderTargetRenderingInfo rendering(
            target, forward_pass().clear_values(),
            cubey::render::RenderTargetAttachmentOps{
                .color = cubey::vulkan::load_store_attachment_ops(),
                .depth = cubey::vulkan::clear_store_attachment_ops(),
            });
        recorder.begin_rendering(rendering.info());
        recorder.set_viewport_and_scissor(target.color.extent);
        std::forward<RecordCallback>(record_callback)(recorder);
        recorder.end_rendering();
    }

    [[nodiscard]] PlanetFrameGraph build_frame_graph(
        cubey::render::ColorTargetView color_target, cubey::render::FrameSlot frame_slot,
        bool present,
        const cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>& instance_buffer,
        std::optional<cubey::render::CloudLayerFrameUniforms> cloud_uniforms) {
        const VkDescriptorSet frame_set = surface_frame_material().set(frame_slot);
        cubey::render::RenderGraphBuilder graph;
        const cubey::render::RenderGraphTextureState initial_state =
            present ? cubey::render::render_graph_undefined_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureState final_state =
            present ? cubey::render::render_graph_present_texture_state()
                    : cubey::render::render_graph_color_attachment_texture_state();
        const cubey::render::RenderGraphTextureHandle backbuffer = graph.import_color_target(
            "planet backbuffer", color_target, initial_state, final_state);
        const cubey::render::RenderGraphTextureHandle scene_color =
            graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "planet scene color", color_target.extent, kPlanetSceneColorFormat));
        cubey::render::RenderGraphTextureHandle post_scene_color = scene_color;
        cubey::render::RenderGraphTextureHandle cloud_scene_color{};
        cubey::render::CloudLayerRuntimeFrame cloud_frame{};
        const cubey::render::RenderGraphTextureHandle depth =
            graph.import_depth_target("planet depth", forward_pass().depth_target(),
                                      cubey::render::render_graph_undefined_texture_state());

        graph.add_pass("planet sky", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .material_pass(cubey::render::atmosphere_background_pass_info())
            .execute([this, scene_color,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                unified_atmosphere_frame_.record_pass(
                    context.recorder(),
                    cubey::render::resolved_color_target_view(context, scene_color), frame_slot);
            });
        graph.add_pass("planet surface", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(depth)
            .material_pass(planet_pass_info())
            .execute([this, scene_color, depth, frame_set,
                      &instance_buffer](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView resolved_color =
                    cubey::render::resolved_color_target_view(context, scene_color);
                const cubey::render::DepthTargetView resolved_depth =
                    cubey::render::resolved_depth_target_view(context, depth);
                record_planet_surface_pass(
                    context.recorder(),
                    cubey::render::render_target_view(resolved_color, resolved_depth),
                    [this, frame_set,
                     &instance_buffer](const cubey::vulkan::CommandRecorder& pass_recorder) {
                        pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                    forward_pass().pipeline().pipeline());
                        pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                          forward_pass().pipeline().layout(), 0,
                                                          frame_set);
                        instance_buffer.bind(pass_recorder, 1);
                        cubey::render::record_draw_item(
                            pass_recorder.handle(), cubey::render::DrawItem{
                                                        .mesh = &patch_grid_mesh(),
                                                        .instance_count = instance_buffer.count(),
                                                    });
                        if (local_detail_draw_enabled()) {
                            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                        local_detail_pipeline().pipeline());
                            pass_recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                              local_detail_pipeline().layout(), 0,
                                                              frame_set);
                            cubey::render::record_draw_item(pass_recorder.handle(),
                                                            cubey::render::DrawItem{
                                                                .mesh = &local_detail_mesh(),
                                                            });
                        }
                    });
            });
        graph.add_pass("planet moon", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(scene_color)
            .write_depth(depth)
            .material_pass(planet_celestial_body_pass_info())
            .execute([this, scene_color, depth,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                const cubey::render::ColorTargetView resolved_color =
                    cubey::render::resolved_color_target_view(context, scene_color);
                const cubey::render::DepthTargetView resolved_depth =
                    cubey::render::resolved_depth_target_view(context, depth);
                record_moon_body_frame(
                    context.recorder(),
                    cubey::render::render_target_view(resolved_color, resolved_depth), frame_slot);
            });
        const bool clouds_enabled = cloud_layer_enabled() && cloud_uniforms.has_value();
        if (clouds_enabled) {
            cloud_scene_color = graph.create_texture(cubey::render::hdr_scene_color_texture_desc(
                "planet cloud scene color", color_target.extent, kPlanetSceneColorFormat));
            cloud_frame = cloud_runtime_.declare_product(
                graph, color_target.extent, planet_cloud_config(cloud_elapsed_seconds_), frame_slot,
                cloud_uniforms.value());
            cloud_runtime_.declare_composite(graph, cloud_scene_color, cloud_frame, frame_slot,
                                             scene_color, depth);
            post_scene_color = cloud_scene_color;
        }
        graph.add_pass("planet post", cubey::render::RenderGraphQueueDomain::Graphics)
            .read_texture(post_scene_color)
            .write_color(backbuffer)
            .material_pass(cubey::render::pbr_post_pass_info())
            .execute([this, color_target,
                      frame_slot](const cubey::render::RenderGraphExecutionContext& context) {
                record_post_pass(context.recorder(), color_target, frame_slot);
            });

        return {
            .graph = graph.compile(),
            .post_scene_color = post_scene_color,
            .scene_color = scene_color,
            .depth = depth,
            .cloud_scene_color = cloud_scene_color,
            .cloud = cloud_frame,
            .clouds_enabled = clouds_enabled,
        };
    }

    void update_post_descriptor(const cubey::vulkan::Device& device,
                                cubey::render::FrameSlot frame_slot,
                                const cubey::render::CompiledRenderGraph& graph,
                                const cubey::render::RenderGraphResourceSet& resources,
                                cubey::render::RenderGraphTextureHandle scene_color) const {
        hdr_post_frame_.update_scene_color_descriptor(device, frame_slot, graph, resources,
                                                      scene_color);
    }

    void record_planet_frame(const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
                             VkCommandBuffer command_buffer,
                             cubey::render::ColorTargetView color_target,
                             cubey::render::FrameSlot frame_slot, bool present) {
        const PlanetSurfaceFrameUniforms uniforms = surface_frame_uniforms(color_target.extent);
        surface_frame_material().upload(frame_slot, uniforms);
        unified_atmosphere_frame_.upload(frame_slot,
                                         unified_atmosphere_frame_uniforms(color_target.extent));
        celestial_body_frame_.upload(frame_slot, moon_body_frame_uniforms(color_target.extent));
        hdr_post_frame_.upload(frame_slot, post_uniforms(color_target.format, color_target.extent));
        refresh_cloud_weather_if_needed(device, gpu);
        std::optional<cubey::render::CloudLayerFrameUniforms> cloud_uniforms{};
        if (cloud_layer_enabled()) {
            cloud_uniforms = cloud_frame_uniforms(color_target.extent);
            cloud_runtime_.upload_frame_uniforms(frame_slot, cloud_uniforms.value());
        }
        const cubey::render::InstanceBuffer<PlanetSurfaceGpuPatchInstance>& instance_buffer =
            surface_runtime_.ensure_instance_buffer(gpu, frame_slot);
        const PlanetFrameGraph frame_graph =
            build_frame_graph(color_target, frame_slot, present, instance_buffer, cloud_uniforms);
        graph_executor_.record(
            cubey::render::RenderGraphFrameRecordInfo{
                .device = &device,
                .command_buffer = command_buffer,
                .frame_slot = frame_slot,
                .label = "vkEndCommandBuffer planet",
                .command_buffer_mode =
                    present ? cubey::render::RenderGraphCommandBufferMode::BeginAndEnd
                            : cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            },
            frame_graph.graph,
            [this, &device, frame_slot,
             &frame_graph](const cubey::render::RenderGraphResourceSet& resources) {
                update_post_descriptor(device, frame_slot, frame_graph.graph, resources,
                                       frame_graph.post_scene_color);
                if (!frame_graph.clouds_enabled) {
                    return;
                }
                cloud_runtime_.update_descriptors(device, frame_slot, frame_graph.graph, resources,
                                                  frame_graph.cloud, frame_graph.scene_color,
                                                  frame_graph.depth);
            });
        if (frame_graph.clouds_enabled) {
            cloud_runtime_.complete_frame(frame_slot, frame_graph.cloud);
        }
    }

    [[nodiscard]] const cubey::render::Mesh& patch_grid_mesh() const {
        if (!patch_grid_mesh_.has_value()) {
            throw std::runtime_error("planet patch grid mesh is not initialized");
        }
        return patch_grid_mesh_.value();
    }

    [[nodiscard]] const cubey::render::Mesh& local_detail_mesh() const {
        if (!local_detail_mesh_.has_value()) {
            throw std::runtime_error("planet local detail mesh is not initialized");
        }
        return local_detail_mesh_.value();
    }

    void create_local_detail_mesh_if_needed(cubey::vulkan::GpuRuntime& gpu) {
        if (local_detail_surface_requested() && !local_detail_mesh_.has_value() &&
            local_detail_runtime_.has_drawable_mesh()) {
            local_detail_mesh_.emplace(gpu, local_detail_runtime_.mesh().mesh_config());
        }
    }

    [[nodiscard]] const cubey::render::Mesh& moon_mesh() const {
        if (!moon_mesh_.has_value()) {
            throw std::runtime_error("planet moon mesh is not initialized");
        }
        return moon_mesh_.value();
    }

    [[nodiscard]] const cubey::render::ForwardScenePass3D& forward_pass() const {
        if (!forward_pass_.has_value()) {
            throw std::runtime_error("planet forward pass is not initialized");
        }
        return forward_pass_.value();
    }

    [[nodiscard]] const cubey::render::GraphicsPipelineResource& local_detail_pipeline() const {
        if (!local_detail_pipeline_.has_value()) {
            throw std::runtime_error("planet local detail pipeline is not initialized");
        }
        return local_detail_pipeline_.value();
    }

    void create_unified_atmosphere_frame_resources_if_needed(const cubey::vulkan::Device& device,
                                                             std::uint32_t frame_slot_count) {
        if (!unified_atmosphere_frame_.materials_created() ||
            unified_atmosphere_frame_.material().material_instance().set_count() !=
                frame_slot_count) {
            unified_atmosphere_frame_.destroy();
            unified_atmosphere_frame_.create_materials(
                device, {
                            .frame_slot_count = frame_slot_count,
                            .textures = sky_atlas_resources_->bindings(),
                        });
        }
    }

    void create_unified_atmosphere_frame_pipeline(const cubey::vulkan::Device& device,
                                                  VkExtent2D extent, VkFormat color_format) {
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("atmosphere.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("atmosphere.frag.spv")),
        };
        unified_atmosphere_frame_.destroy_pipeline();
        unified_atmosphere_frame_.create_pipeline(device, {
                                                              .extent = extent,
                                                              .color_format = color_format,
                                                              .shader_stage_files = shaders,
                                                          });
    }

    void create_cloud_pipelines(const cubey::vulkan::Device& device, VkExtent2D extent,
                                std::uint32_t frame_slot_count) {
        cloud_runtime_.create_swapchain_resources(
            device, cloud_runtime_shader_files(),
            cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth,
            kPlanetSceneColorFormat, extent, frame_slot_count,
            planet_cloud_config(cloud_elapsed_seconds_));
    }

    void create_celestial_body_frame_resources_if_needed(const cubey::vulkan::Device& device,
                                                         std::uint32_t frame_slot_count) {
        if (!celestial_body_frame_.materials_created() ||
            celestial_body_frame_.material().material_instance().set_count() != frame_slot_count) {
            const cubey::render::AtmosphereBackgroundTextureBindings sky_textures =
                sky_atlas_resources_->bindings();
            celestial_body_frame_.destroy();
            celestial_body_frame_.create_materials(
                device, {
                            .frame_slot_count = frame_slot_count,
                            .textures =
                                {
                                    .surface_sampler = sky_textures.lunar_surface_sampler,
                                    .surface_view = sky_textures.lunar_surface_view,
                                    .surface_layout = sky_textures.lunar_surface_layout,
                                },
                        });
        }
    }

    void create_celestial_body_frame_pipeline(const cubey::vulkan::Device& device,
                                              VkExtent2D extent, VkFormat color_format,
                                              VkFormat depth_format) {
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("celestial_body.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("celestial_body.frag.spv")),
        };
        celestial_body_frame_.destroy_pipeline();
        celestial_body_frame_.create_pipeline(device, {
                                                          .extent = extent,
                                                          .color_format = color_format,
                                                          .depth_format = depth_format,
                                                          .shader_stage_files = shaders,
                                                      });
    }

    void create_hdr_post_resources_if_needed(const cubey::vulkan::Device& device,
                                             std::uint32_t frame_slot_count) {
        if (hdr_post_frame_slot_count_ == frame_slot_count) {
            return;
        }
        hdr_post_frame_.destroy();
        hdr_post_frame_.create_materials(device, {
                                                     .frame_slot_count = frame_slot_count,
                                                 });
        hdr_post_frame_slot_count_ = frame_slot_count;
    }

    void create_hdr_post_pipeline(const cubey::vulkan::Device& device, VkExtent2D extent,
                                  VkFormat color_format) {
        const std::array<cubey::render::ShaderStageFile, 2> shaders{
            cubey::render::vertex_shader_file(shader_path("forward_pbr_post.vert.spv")),
            cubey::render::fragment_shader_file(shader_path("forward_pbr_post.frag.spv")),
        };
        hdr_post_frame_.destroy_pipeline();
        hdr_post_frame_.create_pipeline(device, {
                                                    .extent = extent,
                                                    .color_format = color_format,
                                                    .shader_stage_files = shaders,
                                                });
    }

    [[nodiscard]] const cubey::render::FrameUniformMaterialInstance<PlanetSurfaceFrameUniforms>&
    surface_frame_material() const {
        if (!surface_frame_material_.has_value()) {
            throw std::runtime_error("planet surface frame material is not initialized");
        }
        return surface_frame_material_.value();
    }

    RunConfig config_;
    PlanetConfig planet_config_{};
    PlanetConfig edit_planet_config_{};
    PlanetSolarTime solar_time_{};
    PlanetExposureConfig exposure_config_{};
    cubey::render::AtmosphereEnvironmentConfig atmosphere_look_config_{};
    cubey::CloudEnvironmentConfig clouds_config_{};
    PlanetSolarSystemConfig solar_config_{};
    PlanetCelestialSystem celestial_system_{};
    PlanetCelestialLighting celestial_lighting_{};
    PlanetCameraState camera_state_{};
    cubey::Camera3D camera_{cubey::Camera3DConfig{.near_z = 1.0F, .far_z = 1500000.0F}};
    PlanetFrame frame_{};
    PlanetSurfaceRuntime surface_runtime_{};
    PlanetLocalDetailRuntime local_detail_runtime_{};
    bool surface_rebuild_pending_ = false;
    bool camera_interacting_ = false;
    bool planet_config_apply_pending_ = false;
    std::optional<cubey::render::Mesh> patch_grid_mesh_;
    std::optional<cubey::render::Mesh> local_detail_mesh_;
    std::optional<cubey::render::Mesh> moon_mesh_;
    std::optional<cubey::render::AtmosphereBackgroundAtlasResources> sky_atlas_resources_;
    std::optional<cubey::render::FrameUniformMaterialInstance<PlanetSurfaceFrameUniforms>>
        surface_frame_material_;
    std::optional<cubey::render::ForwardScenePass3D> forward_pass_;
    std::optional<cubey::render::GraphicsPipelineResource> local_detail_pipeline_;
    cubey::render::AtmosphereBackgroundFrame unified_atmosphere_frame_{};
    cubey::render::CloudLayerRuntime cloud_runtime_{};
    PlanetCelestialBodyFrame celestial_body_frame_{};
    cubey::render::HdrPostFrame hdr_post_frame_{};
    cubey::render::RenderGraphFrameExecutor graph_executor_;
    cubey::host::FrameStats ui_frame_stats_;
    std::optional<cubey::host::FrameStatsSnapshot> latest_frame_stats_;
    cubey::host::ProcessResourceStatsSampler process_stats_;
    std::string rebuild_error_{};
    std::uint32_t hdr_post_frame_slot_count_ = 0;
    bool cloud_global_resources_created_ = false;
    float cloud_elapsed_seconds_ = 0.0F;
    double latest_fps_ = 0.0;
    double latest_frame_ms_ = 0.0;
};

} // namespace

int run_planet(const RunConfig& config) {
    try {
        PlanetApp app(config);
        return app.run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet: %s\n", error.what());
        return 1;
    }
}

} // namespace cubey::projects::planet
