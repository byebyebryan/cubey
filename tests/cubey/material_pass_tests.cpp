#include <cubey/render/material.h>

#include <vulkan/vulkan.h>

#include <array>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] VkPushConstantRange push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = 64,
    };
}

} // namespace

void test_material_info_defaults_to_depth_and_forward_passes() {
    const cubey::render::MaterialInfo material{};

    require(cubey::render::material_supports_pass(
                material, cubey::render::MaterialPassKind::DepthOnly),
            "default material should support depth-only passes");
    require(cubey::render::material_supports_pass(
                material, cubey::render::MaterialPassKind::ForwardColor),
            "default material should support forward color passes");
}

void test_material_pass_masks_include_requested_passes() {
    const cubey::render::MaterialPassMask forward_only =
        cubey::render::material_pass_mask(cubey::render::MaterialPassKind::ForwardColor);
    const cubey::render::MaterialPassMask both =
        forward_only |
        cubey::render::material_pass_mask(cubey::render::MaterialPassKind::DepthOnly);
    const cubey::render::MaterialInfo material{
        .label = "forward only",
        .pass_mask = forward_only,
    };

    require(!cubey::render::material_supports_pass(
                material, cubey::render::MaterialPassKind::DepthOnly),
            "custom material pass mask should exclude missing passes");
    require(cubey::render::material_supports_pass(
                material, cubey::render::MaterialPassKind::ForwardColor),
            "custom material pass mask should include requested passes");
    require(cubey::render::material_supports_pass(
                both, cubey::render::MaterialPassKind::DepthOnly),
            "combined material pass mask should include depth-only passes");
    require(cubey::render::material_supports_pass(
                both, cubey::render::MaterialPassKind::ForwardColor),
            "combined material pass mask should include forward color passes");
}

void test_material_pass_info_validates_descriptor_and_push_constant_shape() {
    const cubey::render::MaterialPassInfo valid{
        .label = "surface",
        .descriptor_sets =
            {
                cubey::render::MaterialDescriptorSetLayout{
                    .set = 0,
                    .bindings =
                        {
                            cubey::vulkan::DescriptorSetBindingConfig{
                                .binding = 0,
                                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
                            },
                        },
                },
            },
        .push_constants = {push_constant_range()},
    };
    cubey::render::validate_material_pass_info(valid);

    require_throws(
        [] {
            cubey::render::validate_material_pass_info(cubey::render::MaterialPassInfo{});
        },
        "material pass info should require a label");
    require_throws(
        [] {
            cubey::render::validate_material_pass_info(cubey::render::MaterialPassInfo{
                .label = "duplicate sets",
                .descriptor_sets =
                    {
                        cubey::render::MaterialDescriptorSetLayout{.set = 0},
                        cubey::render::MaterialDescriptorSetLayout{.set = 0},
                    },
            });
        },
        "material pass info should reject duplicate descriptor sets");
    require_throws(
        [] {
            cubey::render::validate_material_pass_info(cubey::render::MaterialPassInfo{
                .label = "duplicate bindings",
                .descriptor_sets =
                    {
                        cubey::render::MaterialDescriptorSetLayout{
                            .set = 0,
                            .bindings =
                                {
                                    cubey::vulkan::DescriptorSetBindingConfig{.binding = 1},
                                    cubey::vulkan::DescriptorSetBindingConfig{.binding = 1},
                                },
                        },
                    },
            });
        },
        "material pass info should reject duplicate descriptor bindings");
    require_throws(
        [] {
            cubey::render::validate_material_pass_info(cubey::render::MaterialPassInfo{
                .label = "zero descriptors",
                .descriptor_sets =
                    {
                        cubey::render::MaterialDescriptorSetLayout{
                            .set = 0,
                            .bindings =
                                {
                                    cubey::vulkan::DescriptorSetBindingConfig{
                                        .binding = 0,
                                        .descriptor_count = 0,
                                    },
                                },
                        },
                    },
            });
        },
        "material pass info should reject zero descriptor counts");
    require_throws(
        [] {
            cubey::render::validate_material_pass_info(cubey::render::MaterialPassInfo{
                .label = "zero push constants",
                .push_constants =
                    {
                        VkPushConstantRange{
                            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                            .offset = 0,
                            .size = 0,
                        },
                    },
            });
        },
        "material pass info should reject zero-sized push constants");
}

void test_material_pass_info_applies_graphics_pipeline_state() {
    const cubey::render::MaterialPassInfo pass{
        .label = "transparent forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .cull_mode = VK_CULL_MODE_BACK_BIT,
        .front_face = VK_FRONT_FACE_CLOCKWISE,
        .depth_test = true,
        .depth_write = false,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
    cubey::vulkan::DynamicGraphicsPipelineConfig config;

    cubey::render::apply_material_pass_state(pass, config);

    require(config.topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            "material pass should apply topology");
    require(config.cull_mode == VK_CULL_MODE_BACK_BIT, "material pass should apply cull mode");
    require(config.front_face == VK_FRONT_FACE_CLOCKWISE,
            "material pass should apply front face");
    require(config.depth_test, "material pass should apply depth test");
    require(!config.depth_write, "material pass should apply depth write");
    require(config.depth_compare_op == VK_COMPARE_OP_LESS_OR_EQUAL,
            "material pass should apply depth compare op");
    require(config.blend_enable, "material pass should apply blend enable");
    require(config.src_color_blend_factor == VK_BLEND_FACTOR_SRC_ALPHA,
            "material pass should apply source color blend factor");
    require(config.dst_color_blend_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "material pass should apply destination color blend factor");
}
