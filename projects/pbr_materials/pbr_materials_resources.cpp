#include "pbr_materials_app_internal.h"
#include <iostream>

#include <cubey/asset/hdr_image.h>
#include <cubey/render/generated_ibl.h>
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

cubey::ForwardPbrRenderer3DConfig forward_pbr_renderer_3d_config() {
    return cubey::forward_pbr_renderer_3d_config_from_shader_directory(
        CUBEY_PBR_MATERIALS_SHADER_DIR, {.shadow_extent = kShadowMapSize});
}

[[nodiscard]] cubey::render::PrimitiveMeshData<cubey::render::PbrVertex>
make_pbr_sphere_mesh(float radius) {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> cube =
        cubey::render::make_uv_sphere_position_color_normal_uv_mesh(
            {.radius = radius, .latitude_segments = 64, .longitude_segments = 128});

    cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> result;
    result.vertices.reserve(cube.vertices.size());
    result.indices = cube.indices;
    for (const cubey::render::VertexPositionColorNormalUv& vertex : cube.vertices) {
        const math::Vec3 normal{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
        auto tangent = glm::cross(math::Vec3{0, 1, 0}, normal);
        tangent = glm::length(tangent) < 1e-5F ? math::Vec3{1, 0, 0} : glm::normalize(tangent);
        result.vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            .tangent = {tangent, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return result;
}

cubey::render::PrimitiveMeshData<cubey::render::PbrVertex>
make_beveled_block_mesh(math::Vec3 half = math::Vec3{.25F}, float radius = .045F) {
    render::PrimitiveMeshData<render::PbrVertex> mesh;
    constexpr int n = 28;
    for (int face = 0; face < 6; ++face) {
        const int axis = face / 2;
        const float sign = face % 2 ? 1.0F : -1.0F;
        const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
        for (int y = 0; y <= n; ++y)
            for (int x = 0; x <= n; ++x) {
                math::Vec3 q{0};
                q[axis] = sign * half[axis];
                q[(axis + 1) % 3] = half[(axis + 1) % 3] * (2.0F * static_cast<float>(x) / n - 1);
                q[(axis + 2) % 3] = half[(axis + 2) % 3] * (2.0F * static_cast<float>(y) / n - 1);
                const auto core = glm::clamp(q, -half + radius, half - radius);
                const auto normal = glm::normalize(q - core);
                const auto p = core + normal * radius;
                auto tangent = glm::cross(math::Vec3{0, 1, 0}, normal);
                tangent =
                    glm::length(tangent) < 1e-5F ? math::Vec3{1, 0, 0} : glm::normalize(tangent);
                mesh.vertices.push_back({.position = p, .normal = normal, .tangent = {tangent, 1}});
            }
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                auto a = static_cast<std::uint16_t>(base + y * (n + 1) + x),
                     b = static_cast<std::uint16_t>(a + 1),
                     c = static_cast<std::uint16_t>(a + n + 1),
                     d = static_cast<std::uint16_t>(c + 1);
                mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
            }
    }
    return mesh;
}

render::PrimitiveMeshData<render::PbrVertex> make_cut_block_mesh() {
    render::PrimitiveMeshData<render::PbrVertex> mesh;
    const auto quad = [&](std::array<math::Vec3, 4> p, std::array<math::Vec3, 4> normals) {
        const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
        for (std::size_t i = 0; i < 4; ++i) {
            auto tangent = glm::cross(math::Vec3{0, 1, 0}, normals[i]);
            tangent = glm::length(tangent) < 1e-5F ? math::Vec3{1, 0, 0} : glm::normalize(tangent);
            mesh.vertices.push_back(
                {.position = p[i], .normal = normals[i], .tangent = {tangent, 1}});
        }
        mesh.indices.insert(mesh.indices.end(), {base, static_cast<std::uint16_t>(base + 1),
                                                 static_cast<std::uint16_t>(base + 2), base,
                                                 static_cast<std::uint16_t>(base + 2),
                                                 static_cast<std::uint16_t>(base + 3)});
    };
    const auto edge = [](float angle) {
        const math::Vec2 d{std::cos(angle), std::sin(angle)};
        const float t =
            std::min(.235F / std::max(std::abs(d.x), 1e-6F), .20F / std::max(std::abs(d.y), 1e-6F));
        return d * t;
    };
    constexpr int segments = 128;
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) * 2 * 3.141592654F / segments,
                    b = static_cast<float>(i + 1) * 2 * 3.141592654F / segments;
        const math::Vec2 u{std::cos(a), std::sin(a)}, v{std::cos(b), std::sin(b)};
        const auto outer0 = edge(a), outer1 = edge(b);
        for (float sign : {-1.0F, 1.0F}) {
            const float z = sign * .16F;
            const math::Vec3 n{0, 0, sign};
            quad({math::Vec3{u * .065F, z}, math::Vec3{outer0, z}, math::Vec3{outer1, z},
                  math::Vec3{v * .065F, z}},
                 {n, n, n, n});
        }
        const math::Vec3 n0{-u, 0}, n1{-v, 0};
        quad({math::Vec3{u * .065F, -.16F}, math::Vec3{v * .065F, -.16F},
              math::Vec3{v * .065F, .16F}, math::Vec3{u * .065F, .16F}},
             {n0, n1, n1, n0});
        const math::Vec2 middle = (outer0 + outer1) * .5F;
        const math::Vec3 side = std::abs(middle.x / .235F) > std::abs(middle.y / .20F)
                                    ? math::Vec3{middle.x > 0 ? 1.0F : -1.0F, 0, 0}
                                    : math::Vec3{0, middle.y > 0 ? 1.0F : -1.0F, 0};
        quad({math::Vec3{outer0, -.16F}, math::Vec3{outer1, -.16F}, math::Vec3{outer1, .16F},
              math::Vec3{outer0, .16F}},
             {side, side, side, side});
    }
    return mesh;
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
    for (auto& table : materials_)
        table.initialize(device, {.block_capacity = static_cast<std::uint32_t>(catalog_.size()),
                                  .editable_after_retire = true});
    create_materials();
    create_mesh(gpu);
    create_ibl_resources(device, gpu);
    create_environment(device, gpu, frame_slot_count);
    create_scene();
    if (!config_.common.profile_output_prefix.empty()) {
        whole_profiler_.emplace(device, frame_slot_count, 1);
        pass_profiler_.emplace(device, frame_slot_count, 32);
    }
    forward_pbr_renderer_ =
        &engine_.renderers().create_forward_pbr_renderer_3d(forward_pbr_renderer_3d_config());
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
        make_pbr_sphere_mesh(.25F));
    if (config_.presentation == "library")
        library_sphere_mesh_handle_ = cubey::render::create_primitive_mesh_resource(
            engine_.render_resources(), meshes_, gpu, "pbr_materials.library-sphere",
            make_pbr_sphere_mesh(.075F));
    block_mesh_handle_ = render::create_primitive_mesh_resource(engine_.render_resources(), meshes_,
                                                                gpu, "pbr_materials.beveled-block",
                                                                make_beveled_block_mesh());
    swatch_mesh_handle_ = render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, gpu, "pbr_materials.swatch",
        make_beveled_block_mesh({.25F, .20F, .018F}, .012F));
    cut_block_mesh_handle_ = render::create_primitive_mesh_resource(
        engine_.render_resources(), meshes_, gpu, "pbr_materials.cut-block", make_cut_block_mesh());
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
