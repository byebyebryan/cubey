#include <cubey/render/celestial_body_frame.h>

#include <cubey/render/pass.h>
#include <cubey/render/primitive_mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace cubey::render {
namespace {

enum class CelestialBodyBinding : std::uint32_t {
    FrameUniforms = 0,
    LunarAtlas = 1,
};

[[nodiscard]] constexpr std::uint32_t binding(CelestialBodyBinding binding) noexcept {
    return static_cast<std::uint32_t>(binding);
}

[[nodiscard]] cubey::math::Vec3 normalized_or_up(cubey::math::Vec3 direction) {
    if (glm::dot(direction, direction) <= 0.0F) {
        return {0.0F, 1.0F, 0.0F};
    }
    return glm::normalize(direction);
}

[[nodiscard]] bool finite_positive(float value) {
    return std::isfinite(value) && value > 0.0F;
}

struct SurfaceBasis {
    cubey::math::Vec3 right{1.0F, 0.0F, 0.0F};
    cubey::math::Vec3 up{0.0F, 1.0F, 0.0F};
    cubey::math::Vec3 forward{0.0F, 0.0F, 1.0F};
};

[[nodiscard]] SurfaceBasis moon_surface_basis(const CelestialBody& body,
                                              const CelestialBodyRenderPlacement& placement,
                                              const CelestialBodyFrameInputs& inputs) {
    cubey::math::Vec3 forward = inputs.camera_render_position_m - placement.center_render_m;
    if (glm::dot(forward, forward) <= 0.0F) {
        forward = -body.direction;
    }
    forward = normalized_or_up(forward);

    cubey::math::Vec3 up_seed{0.0F, 1.0F, 0.0F};
    if (std::abs(glm::dot(up_seed, forward)) > 0.96F) {
        up_seed = {1.0F, 0.0F, 0.0F};
    }
    const cubey::math::Vec3 right = glm::normalize(glm::cross(up_seed, forward));
    const cubey::math::Vec3 up = glm::normalize(glm::cross(forward, right));
    return {
        .right = right,
        .up = up,
        .forward = forward,
    };
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] float moon_atmosphere_sky_visibility_factor(
    const CelestialBody& body, const CelestialLighting& lighting,
    const CelestialBodyAtmosphereInputs& atmosphere) {
    if (!finite_positive(atmosphere.planet_radius_m) ||
        atmosphere.atmosphere_outer_radius_m <= atmosphere.planet_radius_m ||
        glm::dot(atmosphere.camera_position_m, atmosphere.camera_position_m) <= 0.0F) {
        return 0.0F;
    }

    const cubey::math::Vec3 camera_up = normalized_or_up(atmosphere.camera_position_m);
    const float atmosphere_height =
        std::max(atmosphere.atmosphere_outer_radius_m - atmosphere.planet_radius_m, 1.0F);
    const float camera_altitude =
        std::max(glm::length(atmosphere.camera_position_m) - atmosphere.planet_radius_m, 0.0F);
    const float inside_atmosphere =
        1.0F - smoothstep(0.75F, 1.20F, camera_altitude / atmosphere_height);
    if (inside_atmosphere <= 0.0F) {
        return 0.0F;
    }

    const cubey::math::Vec3 sun_direction = normalized_or_up(lighting.primary_light_direction);
    const cubey::math::Vec3 moon_direction = normalized_or_up(body.direction);
    const float sun_elevation = glm::dot(sun_direction, camera_up);
    const float moon_elevation = glm::dot(moon_direction, camera_up);
    const float daylight = smoothstep(-0.06F, 0.25F, sun_elevation);
    const float above_horizon = smoothstep(-0.03F, 0.10F, moon_elevation);
    const float separation =
        std::acos(std::clamp(glm::dot(sun_direction, moon_direction), -1.0F, 1.0F));
    const float near_sun = 1.0F - smoothstep(0.18F, 1.05F, separation);
    const float sky_visibility =
        inside_atmosphere * daylight * above_horizon * (0.70F + near_sun * 0.25F);
    return std::clamp(sky_visibility, 0.0F, 0.95F);
}

} // namespace

CelestialBodyFrameUniforms celestial_body_frame_uniforms(
    const CelestialBody& body, const CelestialBodyRenderPlacement& placement,
    const CelestialLighting& lighting, const cubey::math::Mat4& view_projection,
    const CelestialBodyFrameInputs& inputs) {
    const cubey::math::Vec3 light_direction = normalized_or_up(lighting.primary_light_direction);
    const float sky_visibility =
        moon_atmosphere_sky_visibility_factor(body, lighting, inputs.atmosphere);
    const SurfaceBasis surface_basis = moon_surface_basis(body, placement, inputs);
    return {
        .view_projection = view_projection,
        .center_radius =
            {
                placement.center_render_m.x,
                placement.center_render_m.y,
                placement.center_render_m.z,
                placement.visible ? placement.radius_render_m : 0.0F,
            },
        .camera_position_options =
            {
                inputs.camera_render_position_m.x,
                inputs.camera_render_position_m.y,
                inputs.camera_render_position_m.z,
                0.42F,
            },
        .light_direction_intensity =
            {
                light_direction.x,
                light_direction.y,
                light_direction.z,
                lighting.primary_light_intensity,
            },
        .color_phase =
            {
                body.color.r,
                body.color.g,
                body.color.b,
                body.phase_fraction,
            },
        .visibility_atmosphere =
            {
                sky_visibility,
                0.0F,
                0.32F,
                0.0F,
            },
        .surface_basis_right =
            {
                surface_basis.right.x,
                surface_basis.right.y,
                surface_basis.right.z,
                0.0F,
            },
        .surface_basis_up =
            {
                surface_basis.up.x,
                surface_basis.up.y,
                surface_basis.up.z,
                0.0F,
            },
        .surface_basis_forward_options =
            {
                surface_basis.forward.x,
                surface_basis.forward.y,
                surface_basis.forward.z,
                1.0F,
            },
    };
}

MaterialPassInfo celestial_body_pass_info(CelestialBodyDepthMode depth_mode) {
    return {
        .label = "celestial.body",
        .descriptor_sets =
            {
                MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(CelestialBodyBinding::FrameUniforms),
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = binding(CelestialBodyBinding::LunarAtlas),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .depth_test = depth_mode == CelestialBodyDepthMode::TestNoWrite,
        .depth_write = false,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
}

void validate_celestial_body_frame_texture_bindings(
    const CelestialBodyFrameTextureBindings& textures) {
    if (textures.lunar_sampler == VK_NULL_HANDLE || textures.lunar_view == VK_NULL_HANDLE) {
        throw std::runtime_error("celestial body lunar atlas binding is not initialized");
    }
}

void update_celestial_body_frame_material_texture_bindings(
    const cubey::vulkan::Device& device,
    const FrameUniformMaterialInstance<CelestialBodyFrameUniforms>& material,
    const CelestialBodyFrameTextureBindings& textures) {
    validate_celestial_body_frame_texture_bindings(textures);
    for (std::uint32_t slot_index = 0U; slot_index < material.material_instance().set_count();
         ++slot_index) {
        const FrameSlot frame_slot{
            .index = slot_index,
            .count = material.material_instance().set_count(),
        };
        MaterialDescriptorWriter(material.set(frame_slot))
            .uniform_buffer(binding(CelestialBodyBinding::FrameUniforms),
                            material.uniforms().buffer(frame_slot).handle(),
                            material.uniforms().range())
            .combined_image_sampler(binding(CelestialBodyBinding::LunarAtlas),
                                    textures.lunar_sampler, textures.lunar_view,
                                    textures.lunar_layout)
            .update(device);
    }
}

void CelestialBodyFrame::create_materials(const cubey::vulkan::Device& device,
                                          const CelestialBodyFrameMaterialConfig& config) {
    validate_celestial_body_frame_texture_bindings(config.textures);
    material_.emplace(device, FrameUniformMaterialInstanceConfig{
                                  .material_pass = celestial_body_pass_info(),
                                  .descriptor_set = 0,
                                  .frame_slot_count = config.frame_slot_count,
                                  .uniform_binding = binding(CelestialBodyBinding::FrameUniforms),
                                  .sampled_images =
                                      {
                                          SampledImageMaterialBinding{
                                              .binding =
                                                  binding(CelestialBodyBinding::LunarAtlas),
                                              .sampler = config.textures.lunar_sampler,
                                              .image_view = config.textures.lunar_view,
                                              .layout = config.textures.lunar_layout,
                                          },
                                      },
                              });
}

void CelestialBodyFrame::update_texture_bindings(
    const cubey::vulkan::Device& device,
    const CelestialBodyFrameTextureBindings& textures) const {
    update_celestial_body_frame_material_texture_bindings(device, material(), textures);
}

void CelestialBodyFrame::create_pipeline(const cubey::vulkan::Device& device,
                                         const CelestialBodyFramePipelineConfig& config) {
    const VertexInputLayout vertex_input = vertex_position_color_normal_uv_input_layout();
    const std::array descriptor_set_layouts{material().layout()};
    pipeline_.emplace(device, GraphicsPipelineFileResourceConfig{
                                  .extent = config.extent,
                                  .color_format = config.color_format,
                                  .depth_format = config.depth_format,
                                  .shader_stage_files = config.shader_stage_files,
                                  .vertex_bindings = vertex_input.bindings(),
                                  .vertex_attributes = vertex_input.attribute_descriptions(),
                                  .descriptor_set_layouts = descriptor_set_layouts,
                                  .material_pass = celestial_body_pass_info(config.depth_mode),
                              });
}

void CelestialBodyFrame::destroy_pipeline() {
    pipeline_.reset();
}

void CelestialBodyFrame::destroy() {
    destroy_pipeline();
    material_.reset();
}

void CelestialBodyFrame::upload(FrameSlot frame_slot,
                                const CelestialBodyFrameUniforms& uniforms) const {
    material().upload(frame_slot, uniforms);
}

void CelestialBodyFrame::record_pass(const cubey::vulkan::CommandRecorder& recorder,
                                     const RenderTargetView& target, FrameSlot frame_slot,
                                     const Mesh& mesh) const {
    const RenderTargetRenderingInfo rendering(
        target, RenderClearValues{},
        RenderTargetAttachmentOps{
            .color = cubey::vulkan::load_store_attachment_ops(),
            .depth = cubey::vulkan::load_store_attachment_ops(),
        });
    recorder.begin_rendering(rendering.info());
    recorder.set_viewport_and_scissor(target.color.extent);
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().layout(), 0,
                                 material().set(frame_slot));
    record_draw_item(recorder.handle(), {
                                            .mesh = &mesh,
                                        });
    recorder.end_rendering();
}

bool CelestialBodyFrame::materials_created() const noexcept {
    return material_.has_value();
}

const FrameUniformMaterialInstance<CelestialBodyFrameUniforms>& CelestialBodyFrame::material() const {
    if (!material_.has_value()) {
        throw std::runtime_error("celestial body frame material is not initialized");
    }
    return material_.value();
}

const GraphicsPipelineResource& CelestialBodyFrame::pipeline() const {
    if (!pipeline_.has_value()) {
        throw std::runtime_error("celestial body frame pipeline is not initialized");
    }
    return pipeline_.value();
}

} // namespace cubey::render
