#include "gltf_viewer_app_internal.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/render/generated_ibl.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::projects::gltf_viewer {

void GltfViewerApp::create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                                      cubey::vulkan::GpuRuntime& gpu,
                                                      std::uint32_t frame_slot_count) {
    if (scene_ != nullptr) {
        return;
    }

    const std::filesystem::path input = resolved_input_path();
    if (!input.empty()) {
        asset_.emplace(cubey::asset::load_gltf_asset(input));
        create_imported_asset_scene(device, gpu, asset_.value(), frame_slot_count);
    } else {
        create_default_textures(device, gpu);
        create_fallback_material(device, frame_slot_count);
        create_fallback_mesh(gpu);
        scene_bounds_ = {
            .center = {0.0F, 0.0F, 0.0F},
            .half_extent = {1.0F, 1.0F, 1.0F},
        };
        create_fallback_scene();
    }

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
    if (!config_.profile_output_prefix.empty()) {
        gpu_profiler_.emplace(device, frame_slot_count, 16U);
    }
    create_terrain_backdrop_resources(device, gpu, frame_slot_count);
}

void GltfViewerApp::create_terrain_backdrop_resources(const cubey::vulkan::Device& device,
                                                      cubey::vulkan::GpuRuntime& gpu,
                                                      std::uint32_t frame_slot_count) {
    if (!terrain_backdrop_enabled()) {
        return;
    }
    if (!use_atmosphere_environment_source()) {
        throw std::runtime_error("terrain backdrop requires --pbr-environment-source atmosphere");
    }
    const cubey::terrain::PreparedTerrainBackdropProduct prepared =
        cubey::terrain::prepare_raster_terrain_backdrop_product({
            .heightfield_path = config_.terrain.heightfield_path,
            .render_stride =
                config_.terrain.render_stride == 0U ? 3U : config_.terrain.render_stride,
        });
    terrain_baked_foreground_height_m_ = prepared.baked_foreground_height_m;
    terrain_runtime_.create(
        device, gpu, prepared.product,
        {
            .shaders = cubey::terrain_backdrop_runtime_shader_files(CUBEY_GLTF_VIEWER_SHADER_DIR),
            .frame_slot_count = frame_slot_count,
        });
}

bool GltfViewerApp::terrain_backdrop_enabled() const noexcept {
    return !config_.terrain.heightfield_path.empty();
}

bool GltfViewerApp::ocean_backdrop_enabled() const noexcept {
    return config_.ocean.backdrop > 0;
}

void GltfViewerApp::create_imported_asset_scene(const cubey::vulkan::Device& device,
                                                cubey::vulkan::GpuRuntime& gpu,
                                                const cubey::asset::GltfAsset& asset,
                                                std::uint32_t frame_slot_count) {
    scene_ = &engine_.create_scene();
    cubey::SceneTransaction setup = scene().begin_transaction();
    import_result_ = cubey::import_gltf_scene(
        engine_, setup, asset, device, gpu, import_resources_,
        cubey::GltfSceneImportConfig{
            .frame_slot_count = frame_slot_count,
            .deformation_compute_shader = shader_path("gltf_deform.comp.spv"),
            .label_prefix = "gltf_viewer",
        });
    animation_playback_ = {
        .animation_index = config_.gltf.animation_index,
        .speed = config_.gltf.animation_speed,
    };
    animation_sample_.reset();
    scene_bounds_ = import_result_.bounds;
    triangle_count_ = import_result_.triangle_count;
    create_camera_and_light(setup);
    setup.commit();
    if (!asset.animations.empty()) {
        if (animation_playback_.animation_index >= asset.animations.size()) {
            throw std::runtime_error("requested glTF animation index is out of range");
        }
        const cubey::asset::GltfAnimation& animation =
            asset.animations[animation_playback_.animation_index];
        animation_sample_ = cubey::animation::sample_gltf_animation(
            asset, animation, animation_playback_.time_seconds);
        cubey::SceneEditQueue edits = scene().create_edit_queue();
        cubey::apply_gltf_rigid_animation_sample(edits, asset, import_result_,
                                                 animation_sample_.value());
        scene().commit(edits);
    }
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
    if (!config_.pbr.environment_path.empty()) {
        if (!std::filesystem::exists(config_.pbr.environment_path)) {
            throw std::runtime_error("environment HDR does not exist: " +
                                     config_.pbr.environment_path.string());
        }
        return config_.pbr.environment_path;
    }

    const std::filesystem::path sample = bundled_sample_environment_path();
    if (!sample.empty() && std::filesystem::exists(sample)) {
        return sample;
    }
    return {};
}

void GltfViewerApp::create_default_textures(const cubey::vulkan::Device& device,
                                            cubey::vulkan::GpuRuntime& gpu) {
    if (import_resources_.default_textures.has_value()) {
        return;
    }
    import_resources_.default_textures.emplace(
        cubey::render::create_pbr_default_texture_set(device, gpu));
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
    return config_.pbr.environment_source.empty() || config_.pbr.environment_source == "atmosphere";
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
                                             std::uint32_t frame_slot_count) {
    const cubey::render::MaterialHandle material =
        engine_.render_resources().create_material("gltf_viewer.fallback.material");
    import_result_.material_handles.push_back(material);
    import_result_.first_material_handle = material;
    import_resources_.materials.set_factors(material,
                                            cubey::render::PbrMaterialFactors{
                                                .base_color_factor = {0.86F, 0.82F, 0.72F, 1.0F},
                                                .metallic_factor = 0.0F,
                                                .roughness_factor = 0.58F,
                                            });
    import_resources_.materials.emplace_instance(
        material, device,
        cubey::render::FrameUniformMaterialInstanceConfig{
            .material_pass = cubey::render::pbr_forward_pass_info(),
            .descriptor_set = 1,
            .frame_slot_count = frame_slot_count,
            .uniform_binding =
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Uniforms),
            .sampled_images = fallback_material_sampled_images(),
        });
}

std::vector<cubey::render::SampledImageMaterialBinding>
GltfViewerApp::fallback_material_sampled_images() const {
    if (!import_resources_.default_textures.has_value()) {
        throw std::runtime_error("default PBR texture is not initialized");
    }
    return cubey::render::pbr_default_sampled_image_bindings(
        import_resources_.default_textures.value());
}

void GltfViewerApp::create_fallback_mesh(cubey::vulkan::GpuRuntime& gpu) {
    std::vector<cubey::render::PbrVertex> vertices = fallback_cube_vertices();
    std::vector<std::uint32_t> indices = fallback_cube_indices();
    const cubey::render::MeshHandle mesh =
        engine_.render_resources().create_mesh("gltf_viewer.fallback.cube");
    import_resources_.meshes.emplace(
        mesh, gpu,
        cubey::render::indexed_mesh_config(std::span<const cubey::render::PbrVertex>{vertices},
                                           std::span<const std::uint32_t>{indices}));
    import_result_.mesh_handles.push_back(mesh);
    import_resources_.mesh_primitives = {{
        cubey::GltfImportedPrimitive3D{
            .mesh = mesh,
            .material = import_result_.first_material_handle,
            .local_bounds =
                {
                    .center = {0.0F, 0.0F, 0.0F},
                    .half_extent = {1.0F, 1.0F, 1.0F},
                },
        },
    }};
    triangle_count_ = kFallbackCubeTriangleCount;
    import_result_.triangle_count = triangle_count_;
    import_result_.bounds = {
        .center = {0.0F, 0.0F, 0.0F},
        .half_extent = {1.0F, 1.0F, 1.0F},
    };
    import_resources_.active = true;
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
