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

} // namespace

struct TerrainBackdropRuntime::Impl {
    const render::TerrainBackdropProduct* product = nullptr;
    TerrainBackdropRuntimeShaderFiles shaders{};
    render::MaterialPassInfo surface_pass = surface_pass_info();
    std::optional<render::Mesh> center_mesh{};
    std::vector<render::Mesh> sector_meshes{};
    std::optional<render::Texture2D> material_texture{};
    std::optional<render::MaterialInstance> detail_material{};
    std::optional<render::FrameUniformMaterialInstance<TerrainEnvironmentGpuParameters>>
        environment_material{};
    std::optional<render::ShadowMapPass3D> shadow_pass{};
    std::optional<render::GraphicsPipelineResource> surface_pipeline{};
    render::TerrainShadowCacheState shadow_cache{};
    render::TerrainShadowProjection frame_shadow{};
    TerrainBackdropRuntimeFrameInfo frame{};
    TerrainBackdropRuntimeDrawPlan draw_plan{};
    bool resources_created = false;
    bool shadow_update = false;
    bool shadow_sampled = false;
    bool have_translation = false;
    cubey::math::Vec3 previous_translation{};

    void upload_meshes(vulkan::GpuRuntime& gpu, const render::TerrainBackdropProduct& next) {
        center_mesh.reset();
        sector_meshes.clear();
        if (next.center.has_value()) {
            center_mesh.emplace(gpu, next.center->mesh_config());
        }
        sector_meshes.reserve(next.sectors.size());
        for (const render::TerrainBackdropSectorMesh& sector : next.sectors) {
            sector_meshes.emplace_back(gpu, sector.mesh_config());
        }
        product = &next;
        render::invalidate_terrain_shadow_cache(shadow_cache);
        shadow_sampled = false;
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
TerrainBackdropRuntime::TerrainBackdropRuntime(TerrainBackdropRuntime&&) noexcept = default;
TerrainBackdropRuntime&
TerrainBackdropRuntime::operator=(TerrainBackdropRuntime&&) noexcept = default;

void TerrainBackdropRuntime::create(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                    const TerrainBackdropRuntimeCreateInfo& info) {
    if (info.product == nullptr || info.frame_slot_count == 0U) {
        throw std::runtime_error("terrain backdrop runtime requires a product and frame slots");
    }
    destroy();
    impl_->shaders = info.shaders;
    impl_->upload_meshes(gpu, *info.product);

    const VkPushConstantRange shadow_push{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0U,
        .size = sizeof(TerrainShadowPushConstants),
    };
    const std::array shadow_shaders{render::vertex_shader_file(info.shaders.shadow_vertex)};
    const render::VertexInputLayout shadow_input =
        render::vertex_position_only_input_layout(sizeof(render::VertexPositionColorNormalUv));
    impl_->shadow_pass.emplace(
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

    const std::uint64_t derived =
        procedural::derive_seed(info.material_seed, "terrain.backdrop.filtered-detail.v3");
    const TerrainMaterialPushConstants material_push{
        .seed = static_cast<std::uint32_t>(derived ^ (derived >> 32U)),
    };
    impl_->material_texture.emplace(render::create_compute_generated_texture_2d(
        device, gpu,
        {
            .label = "terrain backdrop filtered detail",
            .extent = {kMaterialExtent, kMaterialExtent},
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .mip_levels = kMaterialMipLevels,
            .shader = render::compute_shader_file(info.shaders.material_compute),
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
    impl_->detail_material.emplace(
        device,
        render::MaterialInstanceConfig{.material_pass = impl_->surface_pass, .descriptor_set = 1U});
    render::MaterialDescriptorWriter detail_writer(impl_->detail_material->set());
    detail_writer.combined_image_sampler(0U, impl_->material_texture->sampler().handle(),
                                         impl_->material_texture->view());
    const render::DepthTexture& shadow = impl_->shadow_pass->depth_texture();
    detail_writer.combined_image_sampler(1U, shadow.sampler().handle(), shadow.view(),
                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    detail_writer.update(device);
    impl_->environment_material.emplace(device, render::FrameUniformMaterialInstanceConfig{
                                                    .material_pass = impl_->surface_pass,
                                                    .descriptor_set = 0U,
                                                    .frame_slot_count = info.frame_slot_count,
                                                    .uniform_binding = 0U,
                                                });
    impl_->resources_created = true;
}

void TerrainBackdropRuntime::create_target_resources(
    const vulkan::Device& device, const TerrainBackdropRuntimeTargetInfo& target) {
    if (!created() || target.color_format == VK_FORMAT_UNDEFINED ||
        target.depth_format == VK_FORMAT_UNDEFINED || target.extent.width == 0U ||
        target.extent.height == 0U) {
        throw std::runtime_error("invalid terrain backdrop runtime target");
    }
    const std::array shaders{
        render::vertex_shader_file(impl_->shaders.vertex),
        render::fragment_shader_file(impl_->shaders.fragment),
    };
    const render::VertexInputLayout vertex_input =
        render::vertex_position_color_normal_uv_input_layout();
    const std::array layouts{
        impl_->environment_material->layout(),
        impl_->detail_material->layout(),
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
    impl_->detail_material.reset();
    impl_->material_texture.reset();
    impl_->shadow_pass.reset();
    impl_->center_mesh.reset();
    impl_->sector_meshes.clear();
    impl_->product = nullptr;
    impl_->resources_created = false;
    impl_->shadow_update = false;
    impl_->shadow_sampled = false;
    impl_->have_translation = false;
    render::invalidate_terrain_shadow_cache(impl_->shadow_cache);
}

void TerrainBackdropRuntime::replace_product(vulkan::GpuRuntime& gpu,
                                             const render::TerrainBackdropProduct& product) {
    if (!created()) {
        throw std::runtime_error("terrain backdrop runtime is not created");
    }
    impl_->upload_meshes(gpu, product);
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

    const render::TerrainBackdropProduct& product = *impl_->product;
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
    const scene::Frustum3D frustum = scene::frustum_from_view_projection(info.view_projection);
    const auto visible = [&](const render::TerrainBackdropSectorBounds& bounds) {
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
        impl_->draw_plan.submitted_triangle_count += product.center->triangle_count();
    }
    for (std::size_t index = 0U; index < product.sectors.size(); ++index) {
        if (visible(product.sectors[index].bounds)) {
            ++impl_->draw_plan.submitted_sector_count;
            impl_->draw_plan.submitted_triangle_count += product.sectors[index].triangle_count();
        }
    }
}

void TerrainBackdropRuntime::complete_frame() {
    if (impl_->shadow_update) {
        render::update_terrain_shadow_cache(
            impl_->shadow_cache, impl_->product->diagnostics.content_hash, impl_->frame_shadow);
    }
    impl_->shadow_sampled = true;
}

void TerrainBackdropRuntime::record_shadow_pass(const vulkan::CommandRecorder& recorder) const {
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
            for (const render::Mesh& mesh : impl_->sector_meshes) {
                render::record_draw_item(pass_recorder.handle(), render::DrawItem{.mesh = &mesh});
            }
        });
}

void TerrainBackdropRuntime::record_surface_draws(const vulkan::CommandRecorder& recorder,
                                                  render::FrameSlot frame_slot) const {
    const render::GraphicsPipelineResource& pipeline = *impl_->surface_pipeline;
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline());
    render::bind_material_instance(recorder, pipeline, impl_->environment_material->material(),
                                   frame_slot);
    render::bind_material_instance(recorder, pipeline, *impl_->detail_material);
    const render::TerrainBackdropProduct& product = *impl_->product;
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
            .material_options =
                {impl_->frame.material == TerrainBackdropMaterialMode::FilteredDetail ? 1.0F : 0.0F,
                 0.0F, 0.0F, 0.0F},
            .world_translation = {impl_->frame.world_translation, 0.0F},
        });
    if (impl_->draw_plan.center_visible && impl_->center_mesh.has_value()) {
        render::record_draw_item(recorder.handle(), render::DrawItem{.mesh = &*impl_->center_mesh});
    }
    for (std::size_t index = 0U; index < impl_->sector_meshes.size(); ++index) {
        const render::TerrainBackdropSectorBounds& bounds = product.sectors[index].bounds;
        const scene::Frustum3D frustum =
            scene::frustum_from_view_projection(impl_->frame.view_projection);
        if (scene::intersects(frustum,
                              Bounds3D{
                                  .center = bounds.center + impl_->frame.world_translation,
                                  .half_extent = {(bounds.maximum.x - bounds.minimum.x) * 0.5F,
                                                  (bounds.maximum.y - bounds.minimum.y) * 0.5F,
                                                  (bounds.maximum.z - bounds.minimum.z) * 0.5F},
                              })) {
            render::record_draw_item(recorder.handle(),
                                     render::DrawItem{.mesh = &impl_->sector_meshes[index]});
        }
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
std::uint64_t TerrainBackdropRuntime::material_texture_bytes() const noexcept {
    return ::cubey::material_texture_bytes();
}
std::uint64_t TerrainBackdropRuntime::shadow_caster_triangle_count() const noexcept {
    if (impl_->product == nullptr) {
        return 0U;
    }
    return impl_->product->diagnostics.render_triangle_count -
           impl_->product->diagnostics.center_render_triangle_count;
}

} // namespace cubey
