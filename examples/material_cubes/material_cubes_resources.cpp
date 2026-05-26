#include "material_cubes_app_internal.h"

#include <cubey/asset/hdr_image.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/pbr.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/texture.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#ifndef CUBEY_MATERIAL_CUBES_SHADER_DIR
#error "CUBEY_MATERIAL_CUBES_SHADER_DIR must be defined by the material_cubes CMake target"
#endif

namespace cubey::examples::material_cubes::detail {
namespace {

cubey::ForwardPbrRenderer3DConfig forward_pbr_renderer_3d_config() {
    return cubey::forward_pbr_renderer_3d_config_from_shader_directory(
        CUBEY_MATERIAL_CUBES_SHADER_DIR, {.shadow_extent = kShadowMapSize});
}

struct MaterialVariant {
    cubey::math::Vec4 base_color = kNeutralMaterialBaseColor;
    float roughness = 1.0F;
    float metallic = 0.0F;
};

[[nodiscard]] float material_grid_t(std::uint32_t index, std::uint32_t count) {
    if (count <= 1U) {
        return 0.0F;
    }
    return static_cast<float>(index) / static_cast<float>(count - 1U);
}

[[nodiscard]] MaterialVariant material_variant_for_cell(std::uint32_t row, std::uint32_t column) {
    const float column_t = material_grid_t(column, kMaterialGridColumns);
    const float row_t = material_grid_t(row, kMaterialGridRows);
    return {
        .base_color = kNeutralMaterialBaseColor,
        .roughness = kMinimumRoughness + ((1.0F - kMinimumRoughness) * column_t),
        .metallic = row_t,
    };
}

[[nodiscard]] MaterialVariant material_variant_for_index(std::uint32_t index) {
    return material_variant_for_cell(index / kMaterialGridColumns, index % kMaterialGridColumns);
}

[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> make_pbr_cube_mesh() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_cube_position_color_normal_uv_mesh();

    cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> result;
    result.vertices.reserve(cube.vertices.size());
    result.indices = cube.indices;
    for (const cubey::render::VertexPositionColorNormalUv& vertex : cube.vertices) {
        result.vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return result;
}

} // namespace

void MaterialCubesApp::create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                                         cubey::vulkan::GpuRuntime& gpu,
                                                         std::uint32_t frame_slot_count) {
    if (scene_ != nullptr) {
        return;
    }
    create_default_textures(device, gpu);
    create_materials(device, frame_slot_count);
    create_mesh(gpu);
    create_scene();
    create_ibl_resources(device, gpu);
    forward_pbr_renderer_ =
        &engine_.renderers().create_forward_pbr_renderer_3d(forward_pbr_renderer_3d_config());
    forward_pbr_renderer().create_global_resources(device, ibl_environment(), frame_slot_count);
}

void MaterialCubesApp::create_swapchain_resources(const cubey::vulkan::Device& device,
                                                  VkExtent2D extent, VkFormat color_format) {
    forward_pbr_renderer().create_swapchain_resources(
        device, cubey::ForwardPbrRenderer3DTargetResourcesInfo{
                    .extent = extent,
                    .color_format = color_format,
                    .materials = &materials_,
                });
}

void MaterialCubesApp::destroy_swapchain_resources() {
    engine_.renderers().destroy_swapchain_resources();
}

void MaterialCubesApp::destroy_all_resources() {
    engine_.renderers().destroy_all_resources();
    forward_pbr_renderer_ = nullptr;
    ibl_environment_.reset();
    destroy_scene_if_needed();
    destroy_material_resources();
    destroy_render_handles();
    default_textures_.reset();
}

void MaterialCubesApp::create_default_textures(const cubey::vulkan::Device& device,
                                               cubey::vulkan::GpuRuntime& gpu) {
    if (default_textures_.has_value()) {
        return;
    }
    default_textures_.emplace(cubey::render::create_pbr_default_texture_set(device, gpu));
}

void MaterialCubesApp::create_materials(const cubey::vulkan::Device& device,
                                        std::uint32_t frame_slot_count) {
    material_handles_.reserve(kMaterialCubeCount);
    for (std::uint32_t index = 0; index < kMaterialCubeCount; ++index) {
        const MaterialVariant variant = material_variant_for_index(index);
        const cubey::render::MaterialHandle material =
            engine_.render_resources().create_material(cubey::render::MaterialInfo{
                .label = "material_cubes.material." + std::to_string(index),
                .sort_key = index,
            });
        material_handles_.push_back(material);
        materials_.set_factors(material, cubey::render::PbrMaterialFactors{
                                             .base_color_factor = variant.base_color,
                                             .metallic_factor = variant.metallic,
                                             .roughness_factor = variant.roughness,
                                             .reflectance = 0.5F,
                                         });
        materials_.emplace_instance(material, device,
                                    cubey::render::FrameUniformMaterialInstanceConfig{
                                        .material_pass = cubey::render::pbr_forward_pass_info(),
                                        .descriptor_set = 1,
                                        .frame_slot_count = frame_slot_count,
                                        .uniform_binding = static_cast<std::uint32_t>(
                                            cubey::render::PbrMaterialBinding::Uniforms),
                                        .sampled_images = material_sampled_images(),
                                    });
    }
}

std::vector<cubey::render::SampledImageMaterialBinding>
MaterialCubesApp::material_sampled_images() const {
    if (!default_textures_.has_value()) {
        throw std::runtime_error("material_cubes default PBR texture set is not initialized");
    }
    return cubey::render::pbr_default_sampled_image_bindings(default_textures_.value());
}

void MaterialCubesApp::create_mesh(cubey::vulkan::GpuRuntime& gpu) {
    cube_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, gpu, "material_cubes.cube", make_pbr_cube_mesh());
}

void MaterialCubesApp::create_ibl_resources(const cubey::vulkan::Device& device,
                                            cubey::vulkan::GpuRuntime& gpu) {
    cubey::render::GeneratedPbrEnvironmentConfig ibl_config;
    ibl_config.intensity = config_.pbr.ibl_intensity;

    if (!config_.pbr.environment_path.empty()) {
        if (!std::filesystem::exists(config_.pbr.environment_path)) {
            throw std::runtime_error("environment HDR does not exist: " +
                                     config_.pbr.environment_path.string());
        }
        const cubey::asset::HdrImage image = cubey::asset::load_hdr_image(config_.pbr.environment_path);
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

void MaterialCubesApp::destroy_material_resources() {
    for (const cubey::render::MaterialHandle material : material_handles_) {
        if (materials_.contains_instance(material) || materials_.contains_factors(material)) {
            materials_.erase(material);
        }
        if (engine_.render_resources().is_alive(material)) {
            engine_.render_resources().destroy_material(material);
        }
    }
    material_handles_.clear();
    materials_.clear();
}

void MaterialCubesApp::destroy_render_handles() {
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, cube_mesh_handle_);
}

const cubey::render::GeneratedPbrEnvironment& MaterialCubesApp::ibl_environment() const {
    if (!ibl_environment_.has_value()) {
        throw std::runtime_error("material_cubes PBR IBL environment is not initialized");
    }
    return ibl_environment_.value();
}

cubey::ForwardPbrRenderer3D& MaterialCubesApp::forward_pbr_renderer() const {
    if (forward_pbr_renderer_ == nullptr) {
        throw std::runtime_error("material_cubes forward PBR renderer is not initialized");
    }
    return *forward_pbr_renderer_;
}

} // namespace cubey::examples::material_cubes::detail
