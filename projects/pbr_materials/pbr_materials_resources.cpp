#include "pbr_materials_app_internal.h"
#include <iostream>

#include <cubey/asset/hdr_image.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/material_specimen_mesh.h>
#include <cubey/render/pbr.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/primitive_resource.h>
#include <cubey/render/texture.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#ifndef CUBEY_PBR_MATERIALS_SHADER_DIR
#error "CUBEY_PBR_MATERIALS_SHADER_DIR must be defined by the pbr_materials CMake target"
#endif

namespace cubey::projects::pbr_materials::detail {
namespace {

cubey::ForwardPbrRenderer3DConfig forward_pbr_renderer_3d_config(bool diagnostics) {
    return cubey::forward_pbr_renderer_3d_config_from_shader_directory(
        CUBEY_PBR_MATERIALS_SHADER_DIR,
        {.shadow_extent = kShadowMapSize,
         .scene_color_format =
             diagnostics ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT});
}

} // namespace

void PbrMaterialsApp::create_global_resources_if_needed(const cubey::vulkan::Device& device,
                                                        cubey::vulkan::GpuRuntime& gpu,
                                                        std::uint32_t frame_slot_count) {
    if (scene_ != nullptr) {
        return;
    }
    create_default_textures(device, gpu);
    catalog_ = asset::load_material_catalog(std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) /
                                            "catalog.json");
    materials_.resize(frame_slot_count);
    published_revision_.resize(frame_slot_count, revision_);
    stress_done_.resize(frame_slot_count, false);
    accepted_profile_frames_.resize(frame_slot_count);
    pending_profile_frames_.resize(frame_slot_count);
    for (auto& table : materials_)
        table.initialize(device, {.block_capacity = static_cast<std::uint32_t>(catalog_.size()),
                                  .editable_after_retire = true});
    create_materials();
    if (config_.taa_moving_fixture &&
        (selected_ < 0 ||
         recipes_.at(static_cast<std::size_t>(selected_)).optics.settings.y > 0.0F)) {
        throw std::invalid_argument("the TAA moving fixture requires one focused opaque material");
    }
    create_mesh(gpu);
    create_ibl_resources(device, gpu);
    create_environment(device, gpu, frame_slot_count);
    create_scene();
    std::cout << "pbr_materials: transmission_source=" << config_.transmission_source
              << " hdr_output_format=" << (config_.hdr_output.empty() ? "none" : "RGBA32F") << '\n';
    if (!config_.common.profile_output_prefix.empty()) {
        whole_profiler_.emplace(device, frame_slot_count, 1);
        pass_profiler_.emplace(device, frame_slot_count, 32);
    }
    forward_pbr_renderer_ = &engine_.renderers().create_forward_pbr_renderer_3d(
        forward_pbr_renderer_3d_config(!config_.hdr_output.empty()));
    forward_pbr_renderer().create_global_resources(
        device,
        {.environment_textures = config_.environment == "studio"
                                     ? render::pbr_environment_texture_bindings(ibl_environment())
                                     : atmosphere_.pbr_environment_bindings(ibl_environment()),
         .frame_slot_count = frame_slot_count,
         .atmosphere_background_textures = atmosphere_textures_
                                               ? std::optional{atmosphere_textures_->bindings()}
                                               : std::nullopt});
}

void PbrMaterialsApp::create_swapchain_resources(const cubey::vulkan::Device& device,
                                                 VkExtent2D extent, VkFormat color_format) {
    create_environment_targets(device, extent, static_cast<std::uint32_t>(materials_.size()));
    forward_pbr_renderer().create_swapchain_resources(
        device, cubey::ForwardPbrRenderer3DTargetResourcesInfo{
                    .extent = extent,
                    .color_format = color_format,
                    .materials = &materials_.front(),
                });
}

void PbrMaterialsApp::destroy_swapchain_resources() {
    engine_.renderers().destroy_swapchain_resources();
    atmosphere_.clouds().destroy_surface_target_resources();
}

void PbrMaterialsApp::destroy_all_resources() {
    for (std::uint32_t slot = 0; slot < accepted_profile_frames_.size(); ++slot)
        collect_profile_slot(slot);
    accepted_profile_frames_.clear();
    pending_profile_frames_.clear();
    profile_ = nullptr;
    engine_.renderers().destroy_all_resources();
    forward_pbr_renderer_ = nullptr;
    if (config_.lighting_cycle != "frozen")
        std::cout << "pbr_materials: environment finish sky_generation="
                  << atmosphere_.reflection_probe().snapshot().generation
                  << " cloud_generation=" << atmosphere_.clouds().snapshot().generation << '\n';
    atmosphere_.destroy();
    atmosphere_textures_.reset();
    whole_profiler_.reset();
    pass_profiler_.reset();
    ibl_environment_.reset();
    destroy_scene_if_needed();
    destroy_material_resources();
    destroy_render_handles();
    default_textures_.reset();
}

void PbrMaterialsApp::create_default_textures(const cubey::vulkan::Device& device,
                                              cubey::vulkan::GpuRuntime& gpu) {
    if (default_textures_.has_value()) {
        return;
    }
    default_textures_.emplace(cubey::render::create_pbr_default_texture_set(device, gpu));
}

void PbrMaterialsApp::create_materials() {
    if (!config_.presentation.empty())
        for (const auto& item : presentation_layout(config_.presentation))
            if (std::none_of(catalog_.begin(), catalog_.end(),
                             [&](const auto& e) { return e.id == item.material; }))
                throw std::invalid_argument("presentation recipe missing from catalog: " +
                                            std::string(item.material));
    for (std::size_t i = 0; i < catalog_.size(); ++i) {
        recipes_.push_back(catalog_[i].recipe);
        if (config_.material == catalog_[i].id)
            selected_ = static_cast<int>(i);
    }
    originals_ = recipes_;
    if (config_.material != "gallery" && selected_ < 0)
        throw std::invalid_argument("unknown material");
    if (!config_.recipe_input.empty()) {
        if (selected_ < 0)
            throw std::invalid_argument("recipe input requires a focused --material");
        recipes_.at(static_cast<std::size_t>(selected_)) =
            asset::load_material_recipe(config_.recipe_input);
    }
    if (selected_ >= 0) {
        auto& r = recipes_.at(static_cast<std::size_t>(selected_));
        if (config_.roughness >= 0)
            r.surface.base_roughness.w = config_.roughness;
        if (config_.pattern_scale >= 0) {
            if (r.library.settings.x > 0)
                r.library.settings.z = config_.pattern_scale;
            else
                r.pattern.settings.z = config_.pattern_scale;
        }
        asset::validate_material_recipe(r);
        if ((r.optics.settings.y > 0) !=
            (originals_.at(static_cast<std::size_t>(selected_)).optics.settings.y > 0))
            throw std::invalid_argument(
                "select the matching opaque or glass slot for recipe input");
        if (!config_.recipe_output.empty())
            asset::save_material_recipe(config_.recipe_output, r);
    }
    for (std::size_t i = 0; i < recipes_.size(); ++i) {
        const auto definition = material_definition(i);
        const auto handle =
            engine_.render_resources().create_material(render::pbr_material_info(definition));
        material_handles_.push_back(handle);
        for (auto& table : materials_)
            (void)table.emplace(handle, definition, material_sampled_images());
    }
}

std::vector<cubey::render::SampledImageMaterialBinding>
PbrMaterialsApp::material_sampled_images() const {
    if (!default_textures_.has_value()) {
        throw std::runtime_error("pbr_materials default PBR texture set is not initialized");
    }
    return cubey::render::pbr_default_sampled_image_bindings(default_textures_.value());
}

void PbrMaterialsApp::create_mesh(cubey::vulkan::GpuRuntime& gpu) {
    sphere_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, gpu, "pbr_materials.sphere",
        render::make_material_sphere_mesh(.25F, config_.sphere_density == "dense" ? 128 : 64,
                                          config_.sphere_density == "dense" ? 256 : 128));
    if (config_.presentation == "library")
        library_sphere_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
            engine_.render_resources(), meshes_, gpu, "pbr_materials.library-sphere",
            render::make_material_sphere_mesh(.075F, config_.sphere_density == "dense" ? 128 : 64,
                                              config_.sphere_density == "dense" ? 256 : 128));
    block_mesh_handle_ = render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, gpu, "pbr_materials.beveled-block",
        render::make_material_beveled_block_mesh());
    swatch_mesh_handle_ = render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, gpu, "pbr_materials.swatch",
        render::make_material_beveled_block_mesh({.25F, .20F, .018F}, .012F));
    cut_block_mesh_handle_ = render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, gpu, "pbr_materials.cut-block",
        render::make_material_cut_block_mesh());
}

void PbrMaterialsApp::create_ibl_resources(const cubey::vulkan::Device& device,
                                           cubey::vulkan::GpuRuntime& gpu) {
    cubey::render::GeneratedPbrEnvironmentConfig ibl_config;
    ibl_config.intensity = config_.pbr.ibl_intensity;

    if (config_.pbr.environment_path) {
        if (!std::filesystem::exists(*config_.pbr.environment_path)) {
            throw std::runtime_error("environment HDR does not exist: " +
                                     config_.pbr.environment_path->string());
        }
        const cubey::asset::HdrImage image =
            cubey::asset::load_hdr_image(*config_.pbr.environment_path);
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

void PbrMaterialsApp::destroy_material_resources() {
    for (const auto material : material_handles_)
        if (engine_.render_resources().is_alive(material))
            engine_.render_resources().destroy_material(material);
    material_handles_.clear();
    materials_.clear();
    recipes_.clear();
    originals_.clear();
}

void PbrMaterialsApp::destroy_render_handles() {
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, swatch_mesh_handle_);
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_,
                                         cut_block_mesh_handle_);
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, sphere_mesh_handle_);
    if (library_sphere_mesh_handle_) {
        cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_,
                                             *library_sphere_mesh_handle_);
        library_sphere_mesh_handle_.reset();
    }
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, block_mesh_handle_);
}

const cubey::render::GeneratedPbrEnvironment& PbrMaterialsApp::ibl_environment() const {
    if (!ibl_environment_.has_value()) {
        throw std::runtime_error("pbr_materials PBR IBL environment is not initialized");
    }
    return ibl_environment_.value();
}

cubey::ForwardPbrRenderer3D& PbrMaterialsApp::forward_pbr_renderer() const {
    if (forward_pbr_renderer_ == nullptr) {
        throw std::runtime_error("pbr_materials forward PBR renderer is not initialized");
    }
    return *forward_pbr_renderer_;
}

} // namespace cubey::projects::pbr_materials::detail
