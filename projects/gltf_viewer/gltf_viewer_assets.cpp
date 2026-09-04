#include "gltf_viewer_app_internal.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/render/generated_ibl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::gltf_viewer {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsed_milliseconds(Clock::time_point started) {
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

[[nodiscard]] cubey::GltfSceneImportConfig gltf_import_config(std::uint32_t frame_slot_count) {
    return {
        .frame_slot_count = frame_slot_count,
        .deformation_compute_shader = shader_path("gltf_deform.comp.spv"),
        .label_prefix = "gltf_viewer",
    };
}

} // namespace

void GltfViewerApp::create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                                      cubey::vulkan::GpuRuntime& gpu,
                                                      std::uint32_t frame_slot_count) {
    if (global_resources_created_) {
        return;
    }
    frame_slot_count_ = frame_slot_count;

    requested_input_path_ = resolved_input_path();
    const bool has_requested_input = !requested_input_path_.empty();
    const bool loading_cage = has_requested_input;
    cubey::asset::GltfBounds3D fallback_bounds{
        .center = {0.0F, 0.0F, 0.0F},
        .half_extent = {1.0F, 1.0F, 1.0F},
    };
    if (has_requested_input) {
        try {
            fallback_bounds = cubey::asset::probe_gltf_scene_bounds(requested_input_path_);
        } catch (const std::exception&) {
            // Keep startup resilient and let the staged request publish the
            // canonical parse/validation error. A neutral cage still makes
            // the requested-input state intentional while that request runs.
            fallback_bounds = {
                .center = {0.0F, 0.0F, 0.0F},
                .half_extent = {1.0F, 1.0F, 1.0F},
            };
        } catch (...) {
            fallback_bounds = {
                .center = {0.0F, 0.0F, 0.0F},
                .half_extent = {1.0F, 1.0F, 1.0F},
            };
        }
    }

    // The fallback is intentionally complete before the input is even queued:
    // windowed startup can present it while decoding/transcoding happens off-thread.
    auto fallback = std::make_shared<GltfViewerSceneGeneration>();
    fallback->source = {.id = 0U, .label = "fallback"};
    fallback->source_path = requested_input_path_;
    fallback->bounds = {
        .center = fallback_bounds.center,
        .half_extent = fallback_bounds.half_extent,
    };
    create_default_textures(device, gpu, fallback->import_resources);
    create_fallback_material(device, frame_slot_count, loading_cage, *fallback);
    create_fallback_mesh(gpu, loading_cage, *fallback);
    create_fallback_scene(*fallback);
    active_generation_ = std::move(fallback);

    create_ibl_resources(device, gpu);
    create_atmosphere_background_atlases(device, gpu);
    const bool use_atmosphere_environment = use_atmosphere_environment_source();
    if (use_atmosphere_environment) {
        create_atmosphere_environment_runtime(device, frame_slot_count);
        create_cloud_environment_runtime(device, gpu, frame_slot_count);
    }
    forward_pbr_renderer_ =
        &engine_.renderers().create_forward_pbr_renderer_3d(forward_pbr_renderer_3d_config());
    forward_pbr_renderer().create_global_resources(
        device, cubey::ForwardPbrRenderer3DGlobalResourcesInfo{
                    .environment_textures = pbr_environment_bindings(),
                    .frame_slot_count = frame_slot_count,
                    .atmosphere_background_textures = atmosphere_background_textures(),
                });
    if (!config_.common.profile_output_prefix.empty()) {
        gpu_profiler_.emplace(device, frame_slot_count, 16U);
    }
    create_terrain_backdrop_resources(device, frame_slot_count);
    global_resources_created_ = true;

    // Loading a valid file starts only after fallback/global rendering
    // resources are available.
    if (!requested_input_path_.empty()) {
        request_imported_asset_build(
            requested_input_path_,
            {.supports_texture_compression_bc = device.supports_texture_compression_bc()},
            frame_slot_count);
    }
}

void GltfViewerApp::create_terrain_backdrop_resources(const cubey::vulkan::Device& device,
                                                      std::uint32_t frame_slot_count) {
    if (!terrain_backdrop_enabled()) {
        return;
    }
    if (!use_atmosphere_environment_source()) {
        throw std::runtime_error("terrain backdrop requires --pbr-environment-source atmosphere");
    }
    terrain_runtime_.create(device, {
                                        .shaders = cubey::terrain_backdrop_runtime_shader_files(
                                            CUBEY_GLTF_VIEWER_SHADER_DIR),
                                        .frame_slot_count = frame_slot_count,
                                    });
}

bool GltfViewerApp::terrain_backdrop_enabled() const noexcept {
    return config_.terrain.heightfield_path.has_value();
}

bool GltfViewerApp::ocean_backdrop_enabled() const noexcept {
    return config_.ocean.backdrop.value_or(false);
}

void GltfViewerApp::request_imported_asset_build(std::filesystem::path input,
                                                 cubey::GltfSceneImportCapabilities capabilities,
                                                 std::uint32_t frame_slot_count) {
    const cubey::GltfSceneImportConfig import_config = gltf_import_config(frame_slot_count);
    const std::optional<std::filesystem::path> terrain_path = config_.terrain.heightfield_path;
    const std::uint32_t terrain_stride = config_.terrain.render_stride.value_or(3U);
    const std::string label = input.filename().empty() ? input.string() : input.filename().string();
    static_cast<void>(asset_builds_.request(
        label,
        [input, import_config, capabilities, terrain_path, terrain_stride]() {
            GltfViewerPreparedGeneration prepared;
            prepared.source_path = input;
            prepared.asset = cubey::asset::load_gltf_asset(input);
            prepared.gltf = cubey::prepare_gltf_scene(prepared.asset, import_config, capabilities);
            if (terrain_path.has_value()) {
                prepared.terrain.emplace(cubey::terrain::prepare_raster_terrain_backdrop_product({
                    .heightfield_path = terrain_path.value(),
                    .render_stride = terrain_stride,
                    .foreground_footprint_radius_m = std::hypot(prepared.gltf.bounds.half_extent.x,
                                                                prepared.gltf.bounds.half_extent.z),
                }));
            }
            return prepared;
        },
        [this, import_config](cubey::vulkan::GpuOwnerContext& owner,
                              GltfViewerPreparedGeneration&& prepared) {
            GltfViewerResidentGeneration resident;
            resident.gltf = cubey::build_gltf_scene_resident(owner, prepared.gltf, import_config);
            if (prepared.terrain.has_value()) {
                resident.terrain.emplace(
                    terrain_runtime_.build_resident_product(owner, prepared.terrain->product));
            }
            resident.prepared = std::move(prepared);
            return resident;
        }));
}

void GltfViewerApp::poll_imported_asset_build(cubey::vulkan::GpuRuntime& gpu,
                                              cubey::vulkan::GpuSubmissionTicket retire_after) {
    while (asset_builds_.poll(gpu)) {
    }
    if (asset_builds_.ready()) {
        try {
            activate_imported_asset_generation(gpu, retire_after, asset_builds_.take_ready());
            asset_activation_error_.clear();
        } catch (const std::exception& error) {
            asset_activation_error_ = error.what();
        } catch (...) {
            asset_activation_error_ = "unknown glTF generation activation failure";
        }
    }
    if (asset_builds_.status().phase == cubey::StagedResourcePhase::Failed) {
        asset_activation_error_ = asset_builds_.status().error;
    }
}

void GltfViewerApp::finish_imported_asset_build(cubey::vulkan::GpuRuntime& gpu,
                                                cubey::vulkan::GpuSubmissionTicket retire_after) {
    if (!asset_builds_.busy() && !asset_builds_.ready()) {
        return;
    }
    asset_builds_.finish(gpu);
    if (!asset_builds_.ready()) {
        throw std::runtime_error("glTF staged asset build completed without a resident generation");
    }
    activate_imported_asset_generation(gpu, retire_after, asset_builds_.take_ready());
    asset_activation_error_.clear();
}

void GltfViewerApp::activate_imported_asset_generation(
    cubey::vulkan::GpuRuntime& gpu, cubey::vulkan::GpuSubmissionTicket retire_after,
    cubey::StagedResourceResult<GltfViewerResidentGeneration> resident) {
    const Clock::time_point started = Clock::now();
    GltfViewerResidentGeneration& product = resident.resident;
    if (!product.prepared.asset.animations.empty() &&
        config_.gltf.animation_index >= product.prepared.asset.animations.size()) {
        throw std::runtime_error("requested glTF animation index is out of range");
    }
    if (terrain_backdrop_enabled() != product.prepared.terrain.has_value() ||
        terrain_backdrop_enabled() != product.terrain.has_value()) {
        throw std::runtime_error("glTF terrain staged product is incomplete");
    }

    auto next = std::make_shared<GltfViewerSceneGeneration>();
    next->source = resident.generation;
    next->source_path = product.prepared.source_path;
    next->fallback = false;
    next->bounds = product.prepared.gltf.bounds;
    next->triangle_count = product.prepared.gltf.triangle_count;
    next->animation_playback = {
        .animation_index = config_.gltf.animation_index,
        .speed = config_.gltf.animation_speed,
    };
    const cubey::OrbitController previous_orbit_controller = orbit_controller_;
    const float previous_terrain_foreground_height_m = terrain_foreground_height_m_;
    const float previous_terrain_minimum_foreground_height_m = terrain_minimum_foreground_height_m_;
    const float previous_ocean_foreground_height_m = ocean_foreground_height_m_;
    const float previous_ocean_minimum_foreground_height_m = ocean_minimum_foreground_height_m_;
    const auto restore_active_view_state =
        [this, previous_orbit_controller, previous_terrain_foreground_height_m,
         previous_terrain_minimum_foreground_height_m, previous_ocean_foreground_height_m,
         previous_ocean_minimum_foreground_height_m] {
            orbit_controller_ = previous_orbit_controller;
            terrain_foreground_height_m_ = previous_terrain_foreground_height_m;
            terrain_minimum_foreground_height_m_ = previous_terrain_minimum_foreground_height_m;
            ocean_foreground_height_m_ = previous_ocean_foreground_height_m;
            ocean_minimum_foreground_height_m_ = previous_ocean_minimum_foreground_height_m;
        };
    next->scene = &engine_.create_scene();
    try {
        {
            cubey::SceneTransaction setup = next->scene->begin_transaction();
            next->import_result = cubey::activate_gltf_scene(
                engine_, setup, product.prepared.gltf, std::move(product.gltf),
                next->import_resources, gltf_import_config(frame_slot_count_));
            create_camera_and_light(*next, setup);
            setup.commit();
        }
        next->asset.emplace(std::move(product.prepared.asset));
        if (!next->asset->animations.empty()) {
            const cubey::asset::GltfAnimation& animation =
                next->asset->animations[next->animation_playback.animation_index];
            next->animation_sample = cubey::animation::sample_gltf_animation(
                next->asset.value(), animation, next->animation_playback.time_seconds);
            cubey::SceneEditQueue edits = next->scene->create_edit_queue();
            cubey::apply_gltf_rigid_animation_sample(
                edits, next->asset.value(), next->import_result, next->animation_sample.value());
            next->scene->commit(edits);
        }
        if (product.prepared.terrain.has_value()) {
            next->terrain_surface = cubey::render::BackdropSurfaceEnvelope{
                .nominal_local_height_m =
                    product.prepared.terrain->foreground_surface.nominal_local_height_m,
                .maximum_local_height_m =
                    product.prepared.terrain->foreground_surface.maximum_local_height_m,
            };
            const cubey::render::BackdropSurfacePlacement placement =
                cubey::render::resolve_backdrop_surface_placement({
                    .surface = next->terrain_surface.value(),
                    .foreground =
                        {
                            .anchor_world_height_m = next->bounds.center.y,
                            .minimum_local_height_m = -next->bounds.half_extent.y,
                        },
                    .requested_foreground_height_m = terrain_foreground_height_m_,
                    .minimum_clearance_m = 0.1F,
                });
            if (terrain_target_info_.has_value() && !terrain_runtime_.target_resources_created()) {
                terrain_runtime_.install_resident_product(gpu, std::move(product.terrain.value()),
                                                          retire_after,
                                                          terrain_target_info_.value());
            } else {
                terrain_runtime_.install_resident_product(gpu, std::move(product.terrain.value()),
                                                          retire_after);
            }
            terrain_minimum_foreground_height_m_ = placement.required_foreground_height_m;
            terrain_foreground_height_m_ = placement.effective_foreground_height_m;
        }
    } catch (...) {
        restore_active_view_state();
        destroy_scene_generation(*next);
        cubey::destroy_gltf_scene_import(engine_, next->import_resources, next->import_result);
        throw;
    }

    std::shared_ptr<GltfViewerSceneGeneration> previous = std::move(active_generation_);
    try {
        retire_scene_generation(gpu, retire_after, previous);
    } catch (...) {
        active_generation_ = std::move(previous);
        restore_active_view_state();
        destroy_scene_generation(*next);
        cubey::destroy_gltf_scene_import(engine_, next->import_resources, next->import_result);
        throw;
    }
    active_generation_ = std::move(next);
    asset_activation_milliseconds_ = elapsed_milliseconds(started);
    std::printf(
        "gltf_viewer: activated generation %llu (%s), CPU %.1f ms, GPU %.1f ms, "
        "activation %.1f ms, %u triangles, %llu upload bytes\n",
        static_cast<unsigned long long>(active_generation().source.id),
        active_generation().source.label.c_str(), resident.prepare_milliseconds,
        resident.install_milliseconds, asset_activation_milliseconds_,
        active_generation().triangle_count,
        static_cast<unsigned long long>(active_generation().import_result.mesh_upload_byte_count));
}

void GltfViewerApp::retire_scene_generation(
    cubey::vulkan::GpuRuntime& gpu, cubey::vulkan::GpuSubmissionTicket retire_after,
    const std::shared_ptr<GltfViewerSceneGeneration>& generation) {
    if (!generation) {
        return;
    }
    gpu.defer_destruction_after(retire_after, [engine = &engine_, generation] {
        cubey::destroy_gltf_scene_import(*engine, generation->import_resources,
                                         generation->import_result);
    });
    destroy_scene_generation(*generation);
}

std::filesystem::path GltfViewerApp::resolved_input_path() const {
    if (!config_.gltf.input_path.empty()) {
        if (!std::filesystem::exists(config_.gltf.input_path)) {
            throw std::runtime_error("input glTF asset does not exist: " +
                                     config_.gltf.input_path.string());
        }
        return config_.gltf.input_path;
    }

    const std::filesystem::path sample = bundled_sample_asset_path();
    if (!sample.empty() && std::filesystem::exists(sample)) {
        return sample;
    }
    return {};
}

std::filesystem::path GltfViewerApp::resolved_environment_path() const {
    if (config_.pbr.environment_path) {
        if (!std::filesystem::exists(*config_.pbr.environment_path)) {
            throw std::runtime_error("environment HDR does not exist: " +
                                     config_.pbr.environment_path->string());
        }
        return *config_.pbr.environment_path;
    }

    const std::filesystem::path sample = bundled_sample_environment_path();
    if (!sample.empty() && std::filesystem::exists(sample)) {
        return sample;
    }
    return {};
}

void GltfViewerApp::create_default_textures(const cubey::vulkan::Device& device,
                                            cubey::vulkan::GpuRuntime& gpu,
                                            cubey::GltfSceneImportResources& resources) {
    if (resources.default_textures.has_value()) {
        return;
    }
    resources.default_textures.emplace(cubey::render::create_pbr_default_texture_set(device, gpu));
}

void GltfViewerApp::create_atmosphere_background_atlases(const cubey::vulkan::Device& device,
                                                         cubey::vulkan::GpuRuntime& gpu) {
    if (atmosphere_background_atlases_.created()) {
        return;
    }

    atmosphere_background_atlases_.create(device, gpu, {.night_sky_extent = 128U});
}

void GltfViewerApp::poll_atmosphere_background_atlases(
    const cubey::vulkan::Device& device, cubey::vulkan::GpuRuntime& gpu,
    const cubey::vulkan::FrameResources& frame_resources) {
    const cubey::vulkan::GpuSubmissionTicket retire_after =
        frame_resources.latest_submitted_ticket();
    if (!atmosphere_background_atlases_.poll(gpu, retire_after)) {
        return;
    }
    for (std::uint32_t index = 0U; index < frame_resources.frame_slot_count(); ++index) {
        frame_resources.wait_for_frame(index);
        gpu.mark_submission_completed(frame_resources.submitted_ticket(index));
    }
    const cubey::render::AtmosphereBackgroundTextureBindings textures =
        atmosphere_background_textures();
    forward_pbr_renderer().update_atmosphere_background_texture_bindings(device, textures);
    if (atmosphere_runtime_.resources_created()) {
        atmosphere_runtime_.update_atmosphere_texture_bindings(device, textures);
    }
}

void GltfViewerApp::finish_atmosphere_background_atlases(const cubey::vulkan::Device& device,
                                                         cubey::vulkan::GpuRuntime& gpu) {
    if (!atmosphere_background_atlases_.finish(gpu)) {
        return;
    }
    const cubey::render::AtmosphereBackgroundTextureBindings textures =
        atmosphere_background_textures();
    forward_pbr_renderer().update_atmosphere_background_texture_bindings(device, textures);
    if (atmosphere_runtime_.resources_created()) {
        atmosphere_runtime_.update_atmosphere_texture_bindings(device, textures);
    }
}

cubey::render::AtmosphereBackgroundTextureBindings
GltfViewerApp::atmosphere_background_textures() const {
    if (!atmosphere_background_atlases_.created()) {
        throw std::runtime_error("glTF viewer atmosphere background atlases are not initialized");
    }
    return atmosphere_background_atlases_.bindings();
}

bool GltfViewerApp::use_atmosphere_environment_source() const {
    return !config_.pbr.environment_source || *config_.pbr.environment_source == "atmosphere";
}

cubey::render::PbrEnvironmentTextureBindings GltfViewerApp::pbr_environment_bindings() const {
    if (!use_atmosphere_environment_source()) {
        return cubey::render::pbr_environment_texture_bindings(ibl_environment());
    }
    return atmosphere_runtime_.pbr_environment_bindings(ibl_environment());
}

void GltfViewerApp::create_atmosphere_environment_runtime(const cubey::vulkan::Device& device,
                                                          std::uint32_t frame_slot_count) {
    if (atmosphere_runtime_.resources_created()) {
        return;
    }

    atmosphere_runtime_.create_resources(
        device, cubey::AtmosphereEnvironmentRuntimeResourceConfig{
                    .reflection_extent = 64,
                    .reflection_mip_levels = 5,
                    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .frame_slot_count = frame_slot_count,
                    .atmosphere_textures = atmosphere_background_textures(),
                });
    atmosphere_runtime_.create_pipelines(
        device, cubey::AtmosphereEnvironmentRuntimePipelineConfig{
                    .atmosphere_vertex_shader = shader_path("atmosphere.vert.spv"),
                    .atmosphere_fragment_shader = shader_path("atmosphere.frag.spv"),
                    .reflection_prefilter_vertex_shader = shader_path("atmosphere.vert.spv"),
                    .reflection_prefilter_fragment_shader =
                        shader_path("atmosphere_reflection_prefilter.frag.spv"),
                });
    atmosphere_runtime_.force_reflection_refresh();
}

void GltfViewerApp::create_cloud_environment_runtime(const cubey::vulkan::Device& device,
                                                     cubey::vulkan::GpuRuntime& gpu,
                                                     std::uint32_t frame_slot_count) {
    cubey::CloudEnvironmentRuntime& clouds = atmosphere_runtime_.clouds();
    if (clouds.resources_created()) {
        return;
    }
    const cubey::render::CloudLayerRuntimeShaderFiles shaders =
        cubey::render::cloud_layer_runtime_shader_files(
            CUBEY_GLTF_VIEWER_SHADER_DIR,
            cubey::render::CloudLayerCompositeMode::ExternalBackgroundSceneDepth);
    clouds.create_surface_resources(device, gpu, shaders.generated, cloud_environment_config());
    clouds.create_resources(device,
                            cubey::render::CloudEnvironmentProbeConfig{
                                .extent = 64,
                                .mip_levels = 5,
                                .view_steps = 32,
                                .update_hz = 4.0F,
                                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                .frame_slot_count = frame_slot_count,
                            },
                            clouds.generated_resources(),
                            atmosphere_runtime_.reflection_probe().sky_radiance_cube());
    clouds.create_pipelines(device, cubey::render::CloudEnvironmentProbePipelineConfig{
                                        .cloud_march = shaders.surface_march,
                                        .prefilter_vertex = cubey::render::vertex_shader_file(
                                            shader_path("atmosphere.vert.spv")),
                                        .prefilter_fragment = cubey::render::fragment_shader_file(
                                            shader_path("cloud_environment_prefilter.frag.spv")),
                                    });
}

void GltfViewerApp::create_fallback_material(const cubey::vulkan::Device& device,
                                             std::uint32_t frame_slot_count, bool loading_cage,
                                             GltfViewerSceneGeneration& generation) {
    const cubey::render::MaterialHandle material = engine_.render_resources().create_material(
        loading_cage ? "gltf_viewer.loading_cage.material" : "gltf_viewer.fallback.material");
    generation.import_result.material_handles.push_back(material);
    generation.import_result.first_material_handle = material;
    generation.import_resources.materials.set_factors(
        material,
        cubey::render::PbrMaterialFactors{
            .base_color_factor = loading_cage ? cubey::math::Vec4{0.12F, 0.32F, 0.58F, 1.0F}
                                              : cubey::math::Vec4{0.86F, 0.82F, 0.72F, 1.0F},
            .emissive_factor =
                loading_cage ? cubey::math::Vec3{0.08F, 0.18F, 0.32F} : cubey::math::Vec3{0.0F},
            .metallic_factor = 0.0F,
            .roughness_factor = 0.58F,
            .unlit = loading_cage,
        });
    generation.import_resources.materials.emplace_instance(
        material, device,
        cubey::render::FrameUniformMaterialInstanceConfig{
            .material_pass = cubey::render::pbr_forward_pass_info(),
            .descriptor_set = 1,
            .frame_slot_count = frame_slot_count,
            .uniform_binding =
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Uniforms),
            .sampled_images = cubey::render::pbr_default_sampled_image_bindings(
                generation.import_resources.default_textures.value()),
        });
}

void GltfViewerApp::create_fallback_mesh(cubey::vulkan::GpuRuntime& gpu, bool loading_cage,
                                         GltfViewerSceneGeneration& generation) {
    std::vector<cubey::render::PbrVertex> vertices;
    std::vector<std::uint32_t> indices;
    cubey::asset::GltfBounds3D local_bounds{
        .center = generation.bounds.center,
        .half_extent = generation.bounds.half_extent,
    };
    if (loading_cage) {
        GltfViewerLoadingCageMesh cage = make_gltf_viewer_loading_cage({
            .center = generation.bounds.center,
            .half_extent = generation.bounds.half_extent,
        });
        vertices = std::move(cage.vertices);
        indices = std::move(cage.indices);
        local_bounds = cage.render_bounds;
    } else {
        vertices = fallback_cube_vertices();
        indices = fallback_cube_indices();
    }
    const cubey::render::MeshHandle mesh = engine_.render_resources().create_mesh(
        loading_cage ? "gltf_viewer.loading_cage" : "gltf_viewer.fallback.cube");
    generation.import_resources.meshes.emplace(
        mesh, gpu,
        cubey::render::indexed_mesh_config(std::span<const cubey::render::PbrVertex>{vertices},
                                           std::span<const std::uint32_t>{indices}));
    generation.import_result.mesh_handles.push_back(mesh);
    generation.import_resources.mesh_primitives = {{
        cubey::GltfImportedPrimitive3D{
            .mesh = mesh,
            .material = generation.import_result.first_material_handle,
            .local_bounds =
                {
                    .center = local_bounds.center,
                    .half_extent = local_bounds.half_extent,
                },
        },
    }};
    generation.triangle_count = static_cast<std::uint32_t>(indices.size() / 3U);
    generation.import_result.triangle_count = generation.triangle_count;
    generation.import_result.bounds = {
        .center = generation.bounds.center,
        .half_extent = generation.bounds.half_extent,
    };
    generation.import_resources.active = true;
}

void GltfViewerApp::create_ibl_resources(const cubey::vulkan::Device& device,
                                         cubey::vulkan::GpuRuntime& gpu) {
    cubey::render::GeneratedPbrEnvironmentConfig ibl_config;
    ibl_config.intensity = config_.pbr.ibl_intensity;

    const std::filesystem::path environment = resolved_environment_path();
    if (!environment.empty()) {
        const cubey::asset::HdrImage image = cubey::asset::load_hdr_image(environment);
        ibl_environment_.emplace(cubey::render::create_pbr_environment_from_equirectangular(
            device, gpu,
            cubey::render::PbrEquirectangularImage{
                .width = image.width,
                .height = image.height,
                .rgba32f = image.rgba32f,
            },
            ibl_config));
        return;
    }

    ibl_environment_.emplace(
        cubey::render::create_generated_pbr_environment(device, gpu, ibl_config));
}

} // namespace cubey::projects::gltf_viewer
