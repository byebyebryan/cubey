#include "gltf_viewer_app_internal.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/render/generated_ibl.h>

#include <algorithm>
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
    forward_pbr_renderer_ =
        &engine_.renderers().create_forward_pbr_renderer_3d(forward_pbr_renderer_3d_config());
    forward_pbr_renderer().create_global_resources(device, ibl_environment(), frame_slot_count);
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
        .animation_index = config_.animation_index,
        .speed = config_.animation_speed,
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
    if (!config_.input_path.empty()) {
        if (!std::filesystem::exists(config_.input_path)) {
            throw std::runtime_error("input glTF asset does not exist: " +
                                     config_.input_path.string());
        }
        return config_.input_path;
    }

    const std::filesystem::path sample = bundled_sample_asset_path();
    if (!sample.empty() && std::filesystem::exists(sample)) {
        return sample;
    }
    return {};
}

std::filesystem::path GltfViewerApp::resolved_environment_path() const {
    if (!config_.environment_path.empty()) {
        if (!std::filesystem::exists(config_.environment_path)) {
            throw std::runtime_error("environment HDR does not exist: " +
                                     config_.environment_path.string());
        }
        return config_.environment_path;
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

void GltfViewerApp::create_fallback_material(const cubey::vulkan::Device& device,
                                             std::uint32_t frame_slot_count) {
    const cubey::render::MaterialHandle material =
        engine_.render_resources().create_material("gltf_viewer.fallback.material");
    import_result_.material_handles.push_back(material);
    import_result_.first_material_handle = material;
    import_resources_.materials.set_factors(
        material, cubey::render::PbrMaterialFactors{
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
    ibl_config.intensity = config_.ibl_intensity;

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
