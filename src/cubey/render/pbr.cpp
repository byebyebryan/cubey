#include <cubey/render/pbr.h>

#include <cubey/vulkan/descriptors.h>

namespace cubey::render {

VertexInputLayout pbr_vertex_input_layout() {
    return {
        .vertex_bindings = {vertex_input_binding(0, sizeof(PbrVertex),
                                                 VK_VERTEX_INPUT_RATE_VERTEX)},
        .attributes =
            {
                vertex_input_attribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                       offsetof(PbrVertex, position)),
                vertex_input_attribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                       offsetof(PbrVertex, normal)),
                vertex_input_attribute(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                                       offsetof(PbrVertex, tangent)),
                vertex_input_attribute(3, 0, VK_FORMAT_R32G32_SFLOAT,
                                       offsetof(PbrVertex, uv0)),
            },
    };
}

MaterialPassInfo pbr_forward_pass_info() {
    return pbr_forward_pass_info({});
}

MaterialPassInfo pbr_forward_pass_info(const PbrForwardPassConfig& config) {
    const VkPushConstantRange push_constants{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PbrPushConstants),
    };
    MaterialPassInfo pass{
        .label = config.label.empty()
                     ? (config.blend == MaterialBlendMode::AlphaBlend ? "pbr.forward.alpha"
                                                                      : "pbr.forward")
                     : config.label,
        .kind = MaterialPassKind::ForwardColor,
        .descriptor_sets =
            {
                MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = static_cast<std::uint32_t>(
                                    PbrSceneBinding::SceneUniforms),
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags =
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    static_cast<std::uint32_t>(PbrSceneBinding::ShadowMap),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    static_cast<std::uint32_t>(PbrSceneBinding::IrradianceCube),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    static_cast<std::uint32_t>(PbrSceneBinding::PrefilteredCube),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = static_cast<std::uint32_t>(PbrSceneBinding::BrdfLut),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
                MaterialDescriptorSetLayout{
                    .set = 1,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    static_cast<std::uint32_t>(PbrMaterialBinding::BaseColor),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = static_cast<std::uint32_t>(
                                    PbrMaterialBinding::MetallicRoughness),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    static_cast<std::uint32_t>(PbrMaterialBinding::Normal),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    static_cast<std::uint32_t>(PbrMaterialBinding::Occlusion),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    static_cast<std::uint32_t>(PbrMaterialBinding::Emissive),
                                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding =
                                    static_cast<std::uint32_t>(PbrMaterialBinding::Uniforms),
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
                            },
                        },
                },
            },
        .push_constants = {push_constants},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = true,
    };
    if (config.blend == MaterialBlendMode::AlphaBlend) {
        pass.depth_write = false;
        pass.blend_enable = true;
        pass.src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA;
        pass.dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        pass.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
        pass.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    return pass;
}

PbrMaterialUniforms pbr_material_uniforms(const PbrMaterialFactors& factors) {
    return {
        .base_color_factor = factors.base_color_factor,
        .emissive_alpha_cutoff =
            {
                factors.emissive_factor.x,
                factors.emissive_factor.y,
                factors.emissive_factor.z,
                factors.alpha_cutoff,
            },
        .metallic_roughness_normal_occlusion =
            {
                factors.metallic_factor,
                factors.roughness_factor,
                factors.normal_scale,
                factors.occlusion_strength,
            },
        .specular_color_factor = {1.0F, 1.0F, 1.0F, 1.0F},
        .material_model = {0.5F, 0.0F, 0.0F, 0.0F},
    };
}

PbrPushConstants pbr_push_constants(math::Mat4 model) {
    return {
        .model = model,
    };
}

} // namespace cubey::render
