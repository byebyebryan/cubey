#include "pbr_furnace_app_internal.h"

#include <cubey/render/primitive_resource.h>

#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace cubey::projects::pbr_furnace {

void PbrFurnaceApp::create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                                      cubey::vulkan::GpuRuntime& gpu,
                                                      std::uint32_t frame_slot_count) {
    if (scene_ != nullptr) {
        return;
    }
    create_default_textures(device, gpu);
    white_environment_.emplace(create_white_pbr_environment(device, gpu));
    create_scene_material(device, frame_slot_count);
    create_materials(device, frame_slot_count);
    create_mesh(gpu);
    create_scene();
}

void PbrFurnaceApp::create_default_textures(const cubey::vulkan::Device& device,
                                            cubey::vulkan::GpuRuntime& gpu) {
    if (default_textures_.has_value()) {
        return;
    }
    default_textures_.emplace(cubey::render::create_pbr_default_texture_set(device, gpu));
}

void PbrFurnaceApp::create_scene_material(const cubey::vulkan::Device& device,
                                          std::uint32_t frame_slot_count) {
    const auto binding = [](cubey::render::PbrSceneBinding value) {
        return static_cast<std::uint32_t>(value);
    };
    const WhitePbrEnvironment& environment = white_environment();
    scene_material_.emplace(
        device, cubey::render::FrameUniformMaterialInstanceConfig{
                    .material_pass = cubey::render::pbr_forward_pass_info(),
                    .descriptor_set = 0,
                    .frame_slot_count = frame_slot_count,
                    .uniform_binding = binding(cubey::render::PbrSceneBinding::SceneUniforms),
                    .sampled_images =
                        {
                            cubey::render::SampledImageMaterialBinding{
                                .binding = binding(cubey::render::PbrSceneBinding::ShadowMap),
                                .sampler = dummy_shadow().sampler().handle(),
                                .image_view = dummy_shadow().view(),
                            },
                            cubey::render::SampledImageMaterialBinding{
                                .binding = binding(cubey::render::PbrSceneBinding::IrradianceCube),
                                .sampler = environment.irradiance_cube.sampler().handle(),
                                .image_view = environment.irradiance_cube.view(),
                            },
                            cubey::render::SampledImageMaterialBinding{
                                .binding = binding(cubey::render::PbrSceneBinding::PrefilteredCube),
                                .sampler = environment.prefiltered_cube.sampler().handle(),
                                .image_view = environment.prefiltered_cube.view(),
                            },
                            cubey::render::SampledImageMaterialBinding{
                                .binding = binding(cubey::render::PbrSceneBinding::BrdfLut),
                                .sampler = environment.brdf_lut.sampler().handle(),
                                .image_view = environment.brdf_lut.view(),
                            },
                        },
                });
}

void PbrFurnaceApp::create_materials(const cubey::vulkan::Device& device,
                                     std::uint32_t frame_slot_count) {
    const auto materials = pbr_furnace_material_grid();
    material_handles_.reserve(materials.size());
    for (const PbrFurnaceMaterial& furnace_material : materials) {
        const cubey::render::MaterialHandle material =
            engine_.render_resources().create_material(cubey::render::MaterialInfo{
                .label = "pbr_furnace.material.r" + std::to_string(furnace_material.row) + ".c" +
                         std::to_string(furnace_material.column),
                .sort_key =
                    (furnace_material.row * kPbrFurnaceColumnCount) + furnace_material.column,
        });
        material_handles_.push_back(material);
        materials_.set_factors(material, cubey::render::PbrMaterialFactors{
                                             .base_color_factor = {1.0F, 1.0F, 1.0F, 1.0F},
                                             .metallic_factor = furnace_material.metallic,
                                             .roughness_factor = furnace_material.roughness,
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
PbrFurnaceApp::material_sampled_images() const {
    if (!default_textures_.has_value()) {
        throw std::runtime_error("PBR furnace default texture set is not initialized");
    }
    return cubey::render::pbr_default_sampled_image_bindings(default_textures_.value());
}

void PbrFurnaceApp::create_mesh(cubey::vulkan::GpuRuntime& gpu) {
    const cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> mesh = make_pbr_sphere_mesh();
    sphere_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, gpu, "pbr_furnace.sphere", mesh);
}

void PbrFurnaceApp::destroy_material_resources() {
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

const cubey::render::Texture2D& PbrFurnaceApp::dummy_shadow() const {
    if (!default_textures_.has_value()) {
        throw std::runtime_error("PBR furnace default texture set is not initialized");
    }
    return cubey::render::pbr_default_texture(default_textures_.value(),
                                              cubey::render::PbrMaterialBinding::Occlusion);
}

const WhitePbrEnvironment& PbrFurnaceApp::white_environment() const {
    if (!white_environment_.has_value()) {
        throw std::runtime_error("PBR furnace white environment is not initialized");
    }
    return white_environment_.value();
}

const cubey::render::FrameUniformMaterialInstance<cubey::render::PbrSceneUniforms>&
PbrFurnaceApp::scene_material() const {
    if (!scene_material_.has_value()) {
        throw std::runtime_error("PBR furnace scene material is not initialized");
    }
    return scene_material_.value();
}

const cubey::render::ForwardScenePass3D& PbrFurnaceApp::forward_pass() const {
    if (!forward_pass_.has_value()) {
        throw std::runtime_error("PBR furnace forward pass is not initialized");
    }
    return forward_pass_.value();
}

} // namespace cubey::projects::pbr_furnace
