#include <cubey/engine/terrain_backdrop_runtime.h>

#include <cubey/procedural/seed.h>
#include <cubey/render/generated_texture.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/primitive_mesh.h>
#include <cubey/render/shadow_map.h>
#include <cubey/scene/view_3d.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>

#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey {
namespace {

constexpr std::uint32_t kMaterialExtent = 1'024U;
constexpr std::uint32_t kMaterialMipLevels = 11U;

struct TerrainBackdropPushConstants {
    cubey::math::Mat4 view_projection{1.0F};
    cubey::math::Vec4 camera_position{};
    cubey::math::Vec4 render_options{};
    cubey::math::Vec4 material_options{};
    cubey::math::Vec4 world_translation{};
};

struct TerrainShadowPushConstants {
    cubey::math::Mat4 light_view_projection{1.0F};
};

struct TerrainMaterialPushConstants {
    std::uint32_t seed = 0U;
};

struct TerrainEnvironmentGpuParameters {
    render::AtmosphereEnvironmentFrameUniforms atmosphere{};
    std::array<cubey::math::Vec4, 9> diffuse_irradiance_sh{};
    cubey::math::Vec4 primary_light_direction_intensity{};
    cubey::math::Vec4 primary_light_color_angular_radius{};
    cubey::math::Mat4 light_view_projection{1.0F};
    cubey::math::Vec4 shadow_options{};
};

static_assert(sizeof(TerrainBackdropPushConstants) == 128U);
static_assert(sizeof(TerrainShadowPushConstants) <= 128U);
static_assert(sizeof(TerrainEnvironmentGpuParameters) == sizeof(cubey::math::Vec4) * 33U);

[[nodiscard]] constexpr std::uint64_t material_texture_bytes() noexcept {
    std::uint64_t bytes = 0U;
    std::uint32_t extent = kMaterialExtent;
    for (std::uint32_t mip = 0U; mip < kMaterialMipLevels; ++mip) {
        bytes += static_cast<std::uint64_t>(extent) * extent * 4U;
        extent = extent > 1U ? extent / 2U : 1U;
    }
    return bytes;
}

[[nodiscard]] render::MaterialPassInfo surface_pass_info() {
    constexpr VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    return {
        .label = "terrain.backdrop",
        .descriptor_sets =
            {
                render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
                render::sampled_texture_descriptor_set_layout(1, 2U),
            },
        .push_constants =
            {
                VkPushConstantRange{
                    .stageFlags = stages,
                    .offset = 0U,
                    .size = sizeof(TerrainBackdropPushConstants),
                },
            },
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] TerrainEnvironmentGpuParameters
environment_parameters(const render::AtmosphereEnvironmentFrameUniforms& atmosphere,
                       const render::AtmosphereEnvironmentLighting& lighting,
                       const render::TerrainShadowProjection& shadow, bool shadows_enabled) {
    TerrainEnvironmentGpuParameters result{
        .atmosphere = atmosphere,
        .primary_light_direction_intensity = {lighting.primary_light_direction.x,
                                              lighting.primary_light_direction.y,
                                              lighting.primary_light_direction.z,
                                              lighting.primary_light_intensity},
        .light_view_projection = shadow.light_view_projection,
        .shadow_options = {shadows_enabled && shadow.light_above_horizon ? 1.0F : 0.0F,
                           1.0F / static_cast<float>(render::kTerrainShadowMapExtent),
                           shadow.depth_span_m, shadow.texel_world_size_m},
    };
    for (std::size_t index = 0U; index < result.diffuse_irradiance_sh.size(); ++index) {
        const cubey::math::Vec3 coefficient = lighting.diffuse_irradiance_sh[index];
        result.diffuse_irradiance_sh[index] = {coefficient.x, coefficient.y, coefficient.z, 0.0F};
    }
    const bool sun_is_primary = lighting.sun_intensity >= lighting.moon_intensity;
    const float angular_radius =
        sun_is_primary ? atmosphere.sun_direction_radius.w : atmosphere.moon_direction_radius.w;
    result.primary_light_color_angular_radius = {lighting.primary_light_color.x,
                                                 lighting.primary_light_color.y,
                                                 lighting.primary_light_color.z, angular_radius};
    return result;
}

[[nodiscard]] bool same_translation(cubey::math::Vec3 lhs, cubey::math::Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] float srgb_channel_to_linear(float value) {
    return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] cubey::math::Vec3 srgb_to_linear(cubey::math::Vec3 value) {
    return {srgb_channel_to_linear(value.x), srgb_channel_to_linear(value.y),
            srgb_channel_to_linear(value.z)};
}

struct TerrainBackdropRuntimeSection {
    terrain::TerrainBackdropSectorBounds bounds{};
    std::uint32_t triangle_count = 0U;
};

struct TerrainBackdropRuntimeProductState {
    terrain::TerrainBackdropProductRequest request{};
    terrain::TerrainBackdropSourceInfo source{};
    terrain::TerrainBackdropProductDiagnostics diagnostics{};
    std::optional<TerrainBackdropRuntimeSection> center{};
    std::vector<TerrainBackdropRuntimeSection> sectors{};
};

struct TerrainBackdropRuntimeMeshSet {
    std::optional<render::Mesh> center{};
    std::vector<render::Mesh> sectors{};
};

struct TerrainBackdropRuntimeGeneration {
    TerrainBackdropRuntimeProductState product{};
    TerrainBackdropRuntimeMeshSet meshes{};
    std::optional<render::Texture2D> material_texture{};
    std::optional<render::MaterialInstance> detail_material{};
};

[[nodiscard]] render::MeshConfig
terrain_mesh_config(const terrain::TerrainBackdropSectorMesh& mesh) {
    return render::indexed_mesh_config(
        std::span<const terrain::TerrainBackdropVertex>{mesh.vertices},
        std::span<const std::uint32_t>{mesh.indices});
}

[[nodiscard]] render::VertexInputLayout terrain_vertex_input_layout() {
    return {
        .vertex_bindings = {render::vertex_input_binding(0U, sizeof(terrain::TerrainBackdropVertex),
                                                         VK_VERTEX_INPUT_RATE_VERTEX)},
        .attributes =
            {
                render::vertex_input_attribute(
                    0U, 0U, VK_FORMAT_R32G32B32_SFLOAT,
                    static_cast<std::uint32_t>(offsetof(terrain::TerrainBackdropVertex, position))),
                render::vertex_input_attribute(
                    1U, 0U, VK_FORMAT_R32G32B32_SFLOAT,
                    static_cast<std::uint32_t>(offsetof(terrain::TerrainBackdropVertex, material))),
                render::vertex_input_attribute(
                    2U, 0U, VK_FORMAT_R32G32B32_SFLOAT,
                    static_cast<std::uint32_t>(offsetof(terrain::TerrainBackdropVertex, normal))),
                render::vertex_input_attribute(
                    3U, 0U, VK_FORMAT_R32G32_SFLOAT,
                    static_cast<std::uint32_t>(offsetof(terrain::TerrainBackdropVertex, surface))),
            },
    };
}

[[nodiscard]] TerrainBackdropRuntimeProductState
runtime_product_state(const terrain::TerrainBackdropProduct& product) {
    TerrainBackdropRuntimeProductState result{
        .request = product.request,
        .source = product.source,
        .diagnostics = product.diagnostics,
    };
    if (product.center.has_value()) {
        result.center = TerrainBackdropRuntimeSection{
            .bounds = product.center->bounds,
            .triangle_count = product.center->triangle_count(),
        };
    }
    result.sectors.reserve(product.sectors.size());
    for (const terrain::TerrainBackdropSectorMesh& sector : product.sectors) {
        result.sectors.push_back({
            .bounds = sector.bounds,
            .triangle_count = sector.triangle_count(),
        });
    }
    return result;
}

[[nodiscard]] TerrainBackdropRuntimeMeshSet
upload_mesh_set(vulkan::GpuRuntime& gpu, const terrain::TerrainBackdropProduct& product) {
    TerrainBackdropRuntimeMeshSet result;
    if (product.center.has_value()) {
        result.center.emplace(gpu, terrain_mesh_config(product.center.value()));
    }
    result.sectors.reserve(product.sectors.size());
    for (const terrain::TerrainBackdropSectorMesh& sector : product.sectors) {
        result.sectors.emplace_back(gpu, terrain_mesh_config(sector));
    }
    return result;
}

[[nodiscard]] std::shared_ptr<TerrainBackdropRuntimeGeneration>
build_runtime_generation(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                         const terrain::TerrainBackdropProduct& product,
                         const TerrainBackdropRuntimeShaderFiles& shaders,
                         const render::MaterialPassInfo& surface_pass,
                         const render::ShadowMapPass3D& shadow_pass) {
    auto result = std::make_shared<TerrainBackdropRuntimeGeneration>();
    result->product = runtime_product_state(product);
    result->meshes = upload_mesh_set(gpu, product);

    const std::uint64_t derived =
        procedural::derive_seed(product.source.seed, "terrain.backdrop.filtered-detail.v3");
    const TerrainMaterialPushConstants material_push{
        .seed = static_cast<std::uint32_t>(derived ^ (derived >> 32U)),
    };
    result->material_texture.emplace(render::create_compute_generated_texture_2d(
        device, gpu,
        {
            .label = "terrain backdrop filtered detail",
            .extent = {kMaterialExtent, kMaterialExtent},
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .mip_levels = kMaterialMipLevels,
            .shader = render::compute_shader_file(shaders.material_compute),
            .group_size_x = 8U,
            .group_size_y = 8U,
            .push_constants = std::as_bytes(std::span{&material_push, 1U}),
            .sampler =
                {
                    .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                    .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                    .max_lod = static_cast<float>(kMaterialMipLevels - 1U),
                    .max_anisotropy = 8.0F,
                },
        }));
    result->detail_material.emplace(
        device,
        render::MaterialInstanceConfig{.material_pass = surface_pass, .descriptor_set = 1U});
    render::MaterialDescriptorWriter detail_writer(result->detail_material->set());
    detail_writer.combined_image_sampler(0U, result->material_texture->sampler().handle(),
                                         result->material_texture->view());
    const render::DepthTexture& shadow = shadow_pass.depth_texture();
    detail_writer.combined_image_sampler(1U, shadow.sampler().handle(), shadow.view(),
                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    detail_writer.update(device);
    return result;
}

[[nodiscard]] TerrainBackdropReflection terrain_backdrop_reflection_from_state(
    const terrain::TerrainBackdropProductRequest& request,
    const terrain::TerrainBackdropProductDiagnostics& diagnostics,
    const TerrainBackdropRuntimeFrameInfo& frame) {
    if (!frame.reflections_enabled) {
        return {};
    }
    float rock = std::clamp(diagnostics.mean_rock, 0.0F, 1.0F);
    float snow = std::clamp(diagnostics.mean_snow, 0.0F, 1.0F);
    float ground = std::max(0.0F, 1.0F - rock - snow);
    const float weight_sum = std::max(ground + rock + snow, 0.0001F);
    ground /= weight_sum;
    rock /= weight_sum;
    snow /= weight_sum;
    const float vegetation =
        std::min(std::clamp(diagnostics.mean_vegetation / weight_sum, 0.0F, 1.0F), ground);
    const float soil = ground - vegetation;
    const float moisture = std::clamp(diagnostics.mean_moisture, 0.0F, 1.0F);

    const math::Vec3 vegetation_color =
        glm::mix(srgb_to_linear({0.300F, 0.305F, 0.220F}), srgb_to_linear({0.160F, 0.240F, 0.160F}),
                 moisture);
    const float daylight = glm::smoothstep(-0.10F, 0.08F, frame.atmosphere.sun_direction_radius.y);
    const float snow_response = glm::mix(0.58F, 1.0F, daylight);
    const math::Vec3 base_color = srgb_to_linear({0.295F, 0.285F, 0.265F}) * soil +
                                  vegetation_color * vegetation +
                                  srgb_to_linear({0.385F, 0.370F, 0.350F}) * rock +
                                  srgb_to_linear({0.725F, 0.760F, 0.785F}) * snow * snow_response;

    const math::Vec3 diffuse_irradiance = render::atmosphere_environment_evaluate_sh(
        frame.lighting.diffuse_irradiance_sh, {0.0F, 1.0F, 0.0F});
    const float ndotl = std::max(frame.lighting.primary_light_direction.y, 0.0F);
    const math::Vec3 direct_radiance =
        frame.lighting.primary_light_color * frame.lighting.primary_light_intensity * ndotl;
    const math::Vec3 radiance =
        base_color *
        (glm::max(diffuse_irradiance, math::Vec3{0.0F}) * 0.88F + direct_radiance * 0.318309886F);

    const float representative_height =
        glm::mix(diagnostics.minimum_height_m, diagnostics.maximum_height_m, 0.72F) +
        frame.world_translation.y;
    const float horizon_distance = std::max(request.visible_inner_radius_m, 1'200.0F);
    const float horizon_delta = representative_height - frame.camera_position.y;
    const float horizon_sine = horizon_delta / std::sqrt(horizon_delta * horizon_delta +
                                                         horizon_distance * horizon_distance);
    return {
        .radiance = glm::max(radiance, math::Vec3{0.0F}),
        .strength = 0.78F,
        .horizon_elevation_sine = std::clamp(horizon_sine, -0.08F, 0.28F),
        .horizon_softness = 0.12F,
    };
}

} // namespace

TerrainBackdropReflection
terrain_backdrop_reflection(const terrain::TerrainBackdropProduct& product,
                            const TerrainBackdropRuntimeFrameInfo& frame) {
    return terrain_backdrop_reflection_from_state(product.request, product.diagnostics, frame);
}

struct TerrainBackdropRuntime::Impl {
    const vulkan::Device* device = nullptr;
    std::shared_ptr<TerrainBackdropRuntimeGeneration> generation{};
    TerrainBackdropRuntimeShaderFiles shaders{};
    render::MaterialPassInfo surface_pass = surface_pass_info();
    std::optional<render::FrameUniformMaterialInstance<TerrainEnvironmentGpuParameters>>
        environment_material{};
    std::optional<render::ShadowMapPass3D> shadow_pass{};
    std::optional<render::GraphicsPipelineResource> surface_pipeline{};
    render::TerrainShadowCacheState shadow_cache{};
    render::TerrainShadowProjection frame_shadow{};
    TerrainBackdropRuntimeFrameInfo frame{};
    TerrainBackdropRuntimeDrawPlan draw_plan{};
    std::vector<std::uint32_t> visible_sector_indices{};
    bool resources_created = false;
    bool shadow_update = false;
    bool shadow_sampled = false;
    bool have_translation = false;
    bool frame_prepared = false;
    cubey::math::Vec3 previous_translation{};

    void install(std::shared_ptr<TerrainBackdropRuntimeGeneration> next_generation) noexcept {
        generation = std::move(next_generation);
        render::invalidate_terrain_shadow_cache(shadow_cache);
        draw_plan = {};
        visible_sector_indices.clear();
        shadow_update = false;
        shadow_sampled = false;
        frame_prepared = false;
    }
};

TerrainBackdropRuntimeShaderFiles
terrain_backdrop_runtime_shader_files(const std::filesystem::path& shader_directory) {
    return {
        .vertex = shader_directory / "terrain_backdrop.vert.spv",
        .fragment = shader_directory / "terrain_backdrop_detail.frag.spv",
        .material_compute = shader_directory / "terrain_backdrop_material.comp.spv",
        .shadow_vertex = shader_directory / "terrain_shadow_depth.vert.spv",
    };
}

TerrainBackdropRuntime::TerrainBackdropRuntime() : impl_(std::make_unique<Impl>()) {}
TerrainBackdropRuntime::~TerrainBackdropRuntime() = default;

void TerrainBackdropRuntime::create(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                    const terrain::TerrainBackdropProduct& product,
                                    const TerrainBackdropRuntimeCreateInfo& info) {
    if (info.frame_slot_count == 0U) {
        throw std::runtime_error("terrain backdrop runtime requires frame slots");
    }
    if (created()) {
        throw std::runtime_error("terrain backdrop runtime is already created");
    }
    auto next = std::make_unique<Impl>();
    next->device = &device;
    next->shaders = info.shaders;

    const VkPushConstantRange shadow_push{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0U,
        .size = sizeof(TerrainShadowPushConstants),
    };
    const std::array shadow_shaders{render::vertex_shader_file(info.shaders.shadow_vertex)};
    const render::VertexInputLayout shadow_input =
        render::vertex_position_only_input_layout(sizeof(terrain::TerrainBackdropVertex));
    next->shadow_pass.emplace(
        device,
        render::ShadowMapPass3DConfig{
            .extent = {render::kTerrainShadowMapExtent, render::kTerrainShadowMapExtent},
            .pipeline =
                {
                    .shader_stage_files = shadow_shaders,
                    .vertex_bindings = shadow_input.bindings(),
                    .vertex_attributes = shadow_input.attribute_descriptions(),
                    .material_pass = render::shadow_depth_pass_info({
                        .label = "terrain.shadow",
                        .push_constants = std::span<const VkPushConstantRange>{&shadow_push, 1U},
                        .cull_mode = VK_CULL_MODE_NONE,
                    }),
                },
            .sampler =
                vulkan::SamplerConfig{
                    .min_filter = VK_FILTER_LINEAR,
                    .mag_filter = VK_FILTER_LINEAR,
                    .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                    .border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                    .compare_enable = VK_TRUE,
                    .compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
                    .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                },
        });

    next->generation = build_runtime_generation(device, gpu, product, info.shaders,
                                                next->surface_pass, *next->shadow_pass);
    next->environment_material.emplace(device, render::FrameUniformMaterialInstanceConfig{
                                                   .material_pass = next->surface_pass,
                                                   .descriptor_set = 0U,
                                                   .frame_slot_count = info.frame_slot_count,
                                                   .uniform_binding = 0U,
                                               });
    next->resources_created = true;
    impl_ = std::move(next);
}

void TerrainBackdropRuntime::create_target_resources(
    const vulkan::Device& device, const TerrainBackdropRuntimeTargetInfo& target) {
    if (!created() || target.color_format == VK_FORMAT_UNDEFINED ||
        target.depth_format == VK_FORMAT_UNDEFINED || target.extent.width == 0U ||
        target.extent.height == 0U) {
        throw std::runtime_error("invalid terrain backdrop runtime target");
    }
    if (target_resources_created()) {
        throw std::runtime_error("terrain backdrop runtime target is already created");
    }
    const std::array shaders{
        render::vertex_shader_file(impl_->shaders.vertex),
        render::fragment_shader_file(impl_->shaders.fragment),
    };
    const render::VertexInputLayout vertex_input = terrain_vertex_input_layout();
    const std::array layouts{
        impl_->environment_material->layout(),
        impl_->generation->detail_material->layout(),
    };
    impl_->surface_pipeline.emplace(device,
                                    render::GraphicsPipelineFileResourceConfig{
                                        .extent = target.extent,
                                        .color_format = target.color_format,
                                        .depth_format = target.depth_format,
                                        .shader_stage_files = shaders,
                                        .vertex_bindings = vertex_input.bindings(),
                                        .vertex_attributes = vertex_input.attribute_descriptions(),
                                        .descriptor_set_layouts = layouts,
                                        .material_pass = impl_->surface_pass,
                                    });
}

void TerrainBackdropRuntime::destroy_target_resources() {
    impl_->surface_pipeline.reset();
}

void TerrainBackdropRuntime::destroy() {
    destroy_target_resources();
    impl_->environment_material.reset();
    impl_->shadow_pass.reset();
    impl_->generation.reset();
    impl_->device = nullptr;
    impl_->resources_created = false;
    impl_->shadow_update = false;
    impl_->shadow_sampled = false;
    impl_->have_translation = false;
    impl_->frame_prepared = false;
    impl_->visible_sector_indices.clear();
    render::invalidate_terrain_shadow_cache(impl_->shadow_cache);
}

void TerrainBackdropRuntime::replace_product(vulkan::GpuRuntime& gpu,
                                             const terrain::TerrainBackdropProduct& product,
                                             vulkan::GpuSubmissionTicket retire_after) {
    if (!created()) {
        throw std::runtime_error("terrain backdrop runtime is not created");
    }
    if (impl_->frame_prepared) {
        throw std::runtime_error("terrain backdrop product cannot change during a prepared frame");
    }
    std::shared_ptr<TerrainBackdropRuntimeGeneration> next_generation = build_runtime_generation(
        *impl_->device, gpu, product, impl_->shaders, impl_->surface_pass, *impl_->shadow_pass);
    auto retired_generation = std::make_shared<std::shared_ptr<TerrainBackdropRuntimeGeneration>>();
    *retired_generation = std::move(impl_->generation);
    try {
        gpu.defer_destruction_after(retire_after,
                                    [retired_generation] { retired_generation->reset(); });
    } catch (...) {
        impl_->generation = std::move(*retired_generation);
        throw;
    }
    impl_->install(std::move(next_generation));
}

void TerrainBackdropRuntime::prepare_frame(render::FrameSlot frame_slot,
                                           const TerrainBackdropRuntimeFrameInfo& info) {
    if (!created() || !target_resources_created()) {
        throw std::runtime_error("terrain backdrop runtime resources are incomplete");
    }
    if (impl_->have_translation &&
        !same_translation(impl_->previous_translation, info.world_translation)) {
        render::invalidate_terrain_shadow_cache(impl_->shadow_cache);
    }
    impl_->have_translation = true;
    impl_->previous_translation = info.world_translation;
    impl_->frame = info;

    const TerrainBackdropRuntimeProductState& product = impl_->generation->product;
    const render::TerrainShadowProjection candidate = render::terrain_shadow_projection(
        {
            .outer_radius_m = product.request.outer_radius_m,
            .minimum_height_m = product.diagnostics.minimum_height_m + info.world_translation.y,
            .maximum_height_m = product.diagnostics.maximum_height_m + info.world_translation.y,
        },
        info.lighting.primary_light_direction,
        {info.world_translation.x, info.world_translation.z});
    impl_->shadow_update = render::terrain_shadow_update_required(
        impl_->shadow_cache, info.shadows_enabled, product.diagnostics.content_hash,
        info.lighting.primary_light_direction);
    impl_->frame_shadow = impl_->shadow_update || !impl_->shadow_cache.valid
                              ? candidate
                              : impl_->shadow_cache.projection;
    const bool sample_shadows = info.shadows_enabled && candidate.light_above_horizon &&
                                (impl_->shadow_cache.valid || impl_->shadow_update);
    impl_->environment_material->upload(
        frame_slot, environment_parameters(info.atmosphere, info.lighting, impl_->frame_shadow,
                                           sample_shadows));

    impl_->draw_plan = {};
    impl_->visible_sector_indices.clear();
    impl_->visible_sector_indices.reserve(product.sectors.size());
    const scene::Frustum3D frustum = scene::frustum_from_view_projection(info.view_projection);
    const auto visible = [&](const terrain::TerrainBackdropSectorBounds& bounds) {
        return scene::intersects(frustum,
                                 Bounds3D{
                                     .center = bounds.center + info.world_translation,
                                     .half_extent = {(bounds.maximum.x - bounds.minimum.x) * 0.5F,
                                                     (bounds.maximum.y - bounds.minimum.y) * 0.5F,
                                                     (bounds.maximum.z - bounds.minimum.z) * 0.5F},
                                 });
    };
    if (product.center.has_value() && visible(product.center->bounds)) {
        impl_->draw_plan.center_visible = true;
        impl_->draw_plan.submitted_triangle_count += product.center->triangle_count;
    }
    for (std::size_t index = 0U; index < product.sectors.size(); ++index) {
        if (visible(product.sectors[index].bounds)) {
            impl_->visible_sector_indices.push_back(static_cast<std::uint32_t>(index));
            ++impl_->draw_plan.submitted_sector_count;
            impl_->draw_plan.submitted_triangle_count += product.sectors[index].triangle_count;
        }
    }
    impl_->frame_prepared = true;
}

void TerrainBackdropRuntime::complete_frame() {
    if (!impl_->frame_prepared) {
        throw std::runtime_error("terrain backdrop frame is not prepared");
    }
    if (impl_->shadow_update) {
        render::update_terrain_shadow_cache(impl_->shadow_cache,
                                            impl_->generation->product.diagnostics.content_hash,
                                            impl_->frame_shadow);
    }
    impl_->shadow_sampled = true;
    impl_->frame_prepared = false;
}

void TerrainBackdropRuntime::record_shadow_pass(const vulkan::CommandRecorder& recorder) const {
    if (!impl_->frame_prepared) {
        throw std::runtime_error("terrain backdrop frame is not prepared");
    }
    const render::ShadowMapPass3D& shadow = *impl_->shadow_pass;
    shadow.record(
        recorder, render::depth_clear_value(),
        [this, &shadow](const vulkan::CommandRecorder& pass_recorder) {
            pass_recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        shadow.pipeline().pipeline());
            const cubey::math::Mat4 model =
                glm::translate(cubey::math::Mat4{1.0F}, impl_->frame.world_translation);
            pass_recorder.push_constants(
                shadow.pipeline().layout(), VK_SHADER_STAGE_VERTEX_BIT, 0U,
                TerrainShadowPushConstants{
                    .light_view_projection = impl_->frame_shadow.light_view_projection * model,
                });
            for (const render::Mesh& mesh : impl_->generation->meshes.sectors) {
                render::record_draw_item(pass_recorder.handle(), render::DrawItem{.mesh = &mesh});
            }
        });
}

void TerrainBackdropRuntime::record_surface_draws(const vulkan::CommandRecorder& recorder,
                                                  render::FrameSlot frame_slot) const {
    if (!impl_->frame_prepared) {
        throw std::runtime_error("terrain backdrop frame is not prepared");
    }
    const render::GraphicsPipelineResource& pipeline = *impl_->surface_pipeline;
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
    render::bind_material_instance(recorder, pipeline, impl_->environment_material->material(),
                                   frame_slot);
    render::bind_material_instance(recorder, pipeline, *impl_->generation->detail_material);
    const TerrainBackdropRuntimeProductState& product = impl_->generation->product;
    recorder.push_constants(
        pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U,
        TerrainBackdropPushConstants{
            .view_projection = impl_->frame.view_projection,
            .camera_position = {impl_->frame.camera_position, 0.0F},
            .render_options =
                {static_cast<float>(static_cast<std::uint8_t>(impl_->frame.debug_view)),
                 product.diagnostics.minimum_height_m + impl_->frame.world_translation.y,
                 product.diagnostics.maximum_height_m + impl_->frame.world_translation.y,
                 product.request.visible_inner_radius_m},
            .material_options = {impl_->frame.material ==
                                         render::TerrainBackdropMaterialMode::FilteredDetail
                                     ? 1.0F
                                     : 0.0F,
                                 0.0F, 0.0F, 0.0F},
            .world_translation = {impl_->frame.world_translation, 0.0F},
        });
    if (impl_->draw_plan.center_visible && impl_->generation->meshes.center.has_value()) {
        render::record_draw_item(recorder.handle(),
                                 render::DrawItem{.mesh = &*impl_->generation->meshes.center});
    }
    for (const std::uint32_t index : impl_->visible_sector_indices) {
        render::record_draw_item(
            recorder.handle(), render::DrawItem{.mesh = &impl_->generation->meshes.sectors[index]});
    }
}

void TerrainBackdropRuntime::bind_environment(const vulkan::CommandRecorder& recorder,
                                              const render::GraphicsPipelineResource& pipeline,
                                              render::FrameSlot frame_slot) const {
    render::bind_material_instance(recorder, pipeline, impl_->environment_material->material(),
                                   frame_slot);
}

bool TerrainBackdropRuntime::created() const noexcept {
    return impl_->resources_created;
}
bool TerrainBackdropRuntime::target_resources_created() const noexcept {
    return impl_->surface_pipeline.has_value();
}
bool TerrainBackdropRuntime::shadow_update_this_frame() const noexcept {
    return impl_->shadow_update;
}
bool TerrainBackdropRuntime::shadow_depth_is_sampled() const noexcept {
    return impl_->shadow_sampled;
}
render::DepthTargetView TerrainBackdropRuntime::shadow_depth_target() const {
    return impl_->shadow_pass->depth_target();
}
const render::MaterialPassInfo& TerrainBackdropRuntime::shadow_material_pass() const {
    return impl_->shadow_pass->material_pass();
}
const render::MaterialPassInfo& TerrainBackdropRuntime::surface_material_pass() const {
    return impl_->surface_pass;
}
VkDescriptorSetLayout TerrainBackdropRuntime::environment_layout() const {
    return impl_->environment_material->layout();
}
const render::TerrainShadowProjection& TerrainBackdropRuntime::shadow_projection() const noexcept {
    return impl_->frame_shadow;
}
const render::TerrainShadowCacheState& TerrainBackdropRuntime::shadow_cache() const noexcept {
    return impl_->shadow_cache;
}
const TerrainBackdropRuntimeDrawPlan& TerrainBackdropRuntime::draw_plan() const noexcept {
    return impl_->draw_plan;
}
TerrainBackdropReflection TerrainBackdropRuntime::reflection() const {
    if (impl_->generation == nullptr || !impl_->frame_prepared ||
        !impl_->frame.reflections_enabled) {
        return {};
    }
    return terrain_backdrop_reflection_from_state(
        impl_->generation->product.request, impl_->generation->product.diagnostics, impl_->frame);
}
std::uint64_t TerrainBackdropRuntime::material_texture_bytes() const noexcept {
    return ::cubey::material_texture_bytes();
}
std::uint64_t TerrainBackdropRuntime::shadow_caster_triangle_count() const noexcept {
    if (impl_->generation == nullptr) {
        return 0U;
    }
    return impl_->generation->product.diagnostics.render_triangle_count -
           impl_->generation->product.diagnostics.center_render_triangle_count;
}

} // namespace cubey
