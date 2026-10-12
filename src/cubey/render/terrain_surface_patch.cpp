#include <array>
#include <cmath>
#include <cstddef>
#include <cubey/render/terrain_surface_patch.h>
#include <glm/gtc/matrix_inverse.hpp>

namespace cubey::render {
TerrainSurfacePatchMaterial terrain_surface_patch_macro_material() {
    TerrainSurfacePatchMaterial m;
    m.wet_roughness = 0.78F;
    m.wet_darkening = 0.86F;
    m.terrain_saturation = 0.6F;
    m.terrain_ambient = 2.0F;
    m.terrain_direct = 1.0F;
    m.water_scatter_lighting = 0.7F;
    m.terrain_mineral_scale = 0.62F;
    m.terrain_material_blend = 1.0F;
    m.terrain_normal_strength = 0.12F;
    m.terrain_diffuse_convolution = 1.0F;
    m.daylight_environment = 1.0F;
    m.daylight_exposure = 0.0F;
    m.daylight_sun_scale = 0.45F;
    m.water_wet_normal = 1.0F;
    m.water_ripple_strength = 0.025F;
    m.water_ripple_scale_m = 48.0F;
    m.water_flow_agitation = 1.0F;
    m.water_rain_agitation = 1.0F;
    m.water_cascade_strength = 1.0F;
    m.water_whitewater_strength = 1.0F;
    m.water_whitewater_speed = 2.0F;
    m.water_stream_foam_strength = 0.3F;
    m.water_stream_foam_patchiness = 1.0F;
    m.water_stream_foam_brightness = 0.55F;
    m.water_roughness = 0.14F;
    m.water_scatter_scale = 0.4F;
    m.water_shallow_extinction_boost = 3.0F;
    m.water_shallow_extinction_end_m = 16.0F;
    return m;
}
static_assert(sizeof(TerrainSurfacePatchPush) == 128U);
static_assert(sizeof(TerrainSurfacePatchUniforms) == 400U);
static_assert(offsetof(TerrainSurfacePatchUniforms, terrain_macro) == 240U);
static_assert(offsetof(TerrainSurfacePatchUniforms, water_bank) == 384U);

TerrainSurfacePatchUniforms terrain_surface_patch_uniforms(const TerrainSurfacePatchFrame& f,
                                                           const TerrainSurfacePatchMaterial& m) {
    const bool srgb =
        f.target_format == VK_FORMAT_R8G8B8A8_SRGB || f.target_format == VK_FORMAT_B8G8R8A8_SRGB;
    return {
        glm::inverse(f.view_projection),
        f.shadow_view_projection,
        {f.light_direction, f.exposure},
        {f.light_color, float(f.prefiltered_mip_levels)},
        {float(std::fmod(f.visual_clock_s, 1024.0)), srgb ? 0.0F : 1.0F, float(f.extent.width),
         float(f.extent.height)},
        {m.wet_roughness, m.wet_darkening, m.terrain_saturation, m.terrain_ambient},
        {m.terrain_direct, m.shadow_strength, m.water_roughness, m.water_normal_strength},
        {m.water_extinction_scale, m.water_scatter_scale, m.water_reflection_scale,
         m.water_scatter_lighting},
        {m.water_clarity, m.terrain_mineral_scale, 0.0F, m.terrain_ambient_softening},
        {m.terrain_diffuse_convolution, m.terrain_specular_scale, m.terrain_shadow_scale,
         m.terrain_slope_color_scale},
        {m.film_begin_m, m.film_end_m, m.film_roughness, m.film_ground_mix},
        {float(f.water_view), m.water_wet_normal, m.water_ripple_strength, m.water_ripple_scale_m},
        {float(f.surface_mode), float(f.width), float(f.height), 0.0F},
        {f.daylight ? 1.0F : 0.0F, m.daylight_sun_scale, f.shadow_base_bias, f.shadow_slope_bias},
        {m.water_shallow_extinction_boost, m.water_shallow_extinction_end_m,
         m.water_shallow_coverage_strength, m.water_shallow_coverage_end_m},
        {m.water_flow_agitation, m.water_rain_agitation, f.rain_response,
         float(std::fmod(f.rain_clock_s, 1024.0))},
        {m.water_rapid_strength, m.water_rapid_scale_m, m.water_cascade_strength,
         m.water_landing_strength},
        {m.water_stream_foam_strength, m.water_stream_foam_patchiness,
         m.water_stream_foam_brightness, 0.0F},
        {m.water_bank_irregularity_m, m.water_bank_motion_m, m.water_bank_scale_m,
         m.water_bank_band_m}};
}
MaterialPassInfo terrain_surface_patch_frame_material() {
    MaterialPassInfo result{.label = "terrain.surface_patch.frame"};
    result.descriptor_sets.push_back({.set = 1U});
    auto& b = result.descriptor_sets.back().bindings;
    b.push_back({.binding = 0U,
                 .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT});
    for (std::uint32_t i = 1U; i <= 14U; ++i) {
        const bool storage = i == 9U || i == 14U;
        b.push_back({.binding = i,
                     .type = storage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                     : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                     .stage_flags = static_cast<VkShaderStageFlags>(
                         i == 9U ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                                 : VK_SHADER_STAGE_FRAGMENT_BIT)});
    }
    return result;
}
MaterialPassInfo terrain_surface_patch_mesh_pass(bool water) {
    MaterialPassInfo result{.label = "terrain.surface_patch.mesh"};
    result.push_constants.push_back(
        {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
         .offset = 0U,
         .size = sizeof(TerrainSurfacePatchPush)});
    result.depth_test = !water;
    result.depth_write = !water;
    result.depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
    result.blend_enable = water;
    result.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
    result.dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    result.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
    result.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    return result;
}
struct TerrainSurfacePatchRenderer::State {
    std::array<std::unique_ptr<GraphicsPipelineResource>, 4> ground;
    std::array<std::unique_ptr<GraphicsPipelineResource>, 2> water, shadow;
};
TerrainSurfacePatchRenderer::TerrainSurfacePatchRenderer(const vulkan::Device& device,
                                                         VkExtent2D extent,
                                                         VkDescriptorSetLayout fields,
                                                         VkDescriptorSetLayout frame,
                                                         const std::filesystem::path& directory)
    : state_(std::make_unique<State>()) {
    const std::array<VkDescriptorSetLayout, 2> layouts{fields, frame};
    const auto mesh = [&](const char* vertex, const char* fragment, bool water) {
        const std::array<ShaderStageFile, 2> stages{vertex_shader_file(directory / vertex),
                                                    fragment_shader_file(directory / fragment)};
        return std::make_unique<GraphicsPipelineResource>(
            device, GraphicsPipelineFileResourceConfig{
                        .extent = extent,
                        .color_format = VK_FORMAT_R16G16B16A16_SFLOAT,
                        .depth_format = water ? VK_FORMAT_UNDEFINED : VK_FORMAT_D32_SFLOAT,
                        .shader_stage_files = stages,
                        .descriptor_set_layouts = layouts,
                        .material_pass = terrain_surface_patch_mesh_pass(water)});
    };
    const std::array<const char*, 2> terrain_vertex{"fluid_25d_terrain.vert.spv",
                                                    "fluid_25d_terrain_bspline.vert.spv"};
    const std::array<const char*, 2> water_vertex{"fluid_25d_water.vert.spv",
                                                  "fluid_25d_water_bspline.vert.spv"};
    for (std::size_t i = 0; i < 2U; ++i) {
        state_->ground[i] = mesh(terrain_vertex[i], "fluid_25d_scenic_terrain.frag.spv", false);
        state_->ground[i + 2U] =
            mesh(terrain_vertex[i], "fluid_25d_scenic_terrain_legacy.frag.spv", false);
        state_->water[i] = mesh(water_vertex[i], "fluid_25d_scenic_water.frag.spv", true);
        const std::array<ShaderStageFile, 1> stages{
            vertex_shader_file(directory / terrain_vertex[i])};
        auto pass = terrain_surface_patch_mesh_pass();
        pass.kind = MaterialPassKind::DepthOnly;
        state_->shadow[i] = std::make_unique<GraphicsPipelineResource>(
            device, GraphicsPipelineFileResourceConfig{.extent = {2048U, 2048U},
                                                       .color_format = VK_FORMAT_UNDEFINED,
                                                       .depth_format = VK_FORMAT_D32_SFLOAT,
                                                       .shader_stage_files = stages,
                                                       .descriptor_set_layouts = layouts,
                                                       .material_pass = pass});
    }
}
TerrainSurfacePatchRenderer::~TerrainSurfacePatchRenderer() = default;
namespace {
void draw(const vulkan::CommandRecorder& r, const GraphicsPipelineResource& p,
          VkDescriptorSet fields, VkDescriptorSet frame, const TerrainSurfacePatchPush& push,
          std::uint32_t vertices) {
    r.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline());
    r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout(), 0U, fields);
    r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout(), 1U, frame);
    r.push_constants(p.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0U,
                     push);
    r.draw(vertices);
}
} // namespace
void TerrainSurfacePatchRenderer::ground(const vulkan::CommandRecorder& r, VkDescriptorSet fields,
                                         VkDescriptorSet frame, const TerrainSurfacePatchPush& push,
                                         std::uint32_t vertices, bool bspline, bool legacy) const {
    draw(r, *state_->ground[std::size_t(bspline) + (legacy ? 2U : 0U)], fields, frame, push,
         vertices);
}
void TerrainSurfacePatchRenderer::water(const vulkan::CommandRecorder& r, VkDescriptorSet fields,
                                        VkDescriptorSet frame, const TerrainSurfacePatchPush& push,
                                        std::uint32_t vertices, bool bspline) const {
    draw(r, *state_->water[std::size_t(bspline)], fields, frame, push, vertices);
}
void TerrainSurfacePatchRenderer::shadow(const vulkan::CommandRecorder& r, VkDescriptorSet fields,
                                         VkDescriptorSet frame, const TerrainSurfacePatchPush& push,
                                         std::uint32_t vertices, bool bspline) const {
    draw(r, *state_->shadow[std::size_t(bspline)], fields, frame, push, vertices);
}
} // namespace cubey::render
