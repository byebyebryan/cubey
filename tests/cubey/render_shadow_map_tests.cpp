#include <cubey/render/shadow_map.h>

#include <vulkan/vulkan.h>

#include <span>
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

} // namespace

void test_shadow_map_sampler_uses_depth_texture_defaults() {
    const cubey::vulkan::SamplerConfig config = cubey::render::shadow_map_sampler_config();

    require(config.min_filter == VK_FILTER_NEAREST, "shadow sampler should use nearest min");
    require(config.mag_filter == VK_FILTER_NEAREST, "shadow sampler should use nearest mag");
    require(config.address_mode == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            "shadow sampler should clamp outside the map");
    require(config.border_color == VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            "shadow sampler should treat outside the map as lit");
    require(config.mipmap_mode == VK_SAMPLER_MIPMAP_MODE_NEAREST,
            "shadow sampler should avoid mip filtering");
}

void test_shadow_map_depth_texture_config_describes_sampled_depth_target() {
    const cubey::render::DepthTextureConfig texture =
        cubey::render::shadow_map_depth_texture_config(
            {
                .extent = {2048, 1024},
            },
            VK_FORMAT_D32_SFLOAT);

    require(texture.extent.width == 2048, "shadow depth config should preserve width");
    require(texture.extent.height == 1024, "shadow depth config should preserve height");
    require(texture.format == VK_FORMAT_D32_SFLOAT,
            "shadow depth config should preserve depth format");
    require(texture.create_sampler, "shadow depth texture should be sampled");
    require(texture.sampler.address_mode == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            "shadow depth config should use shadow sampler defaults");

    require_throws(
        [] {
            (void)cubey::render::shadow_map_depth_texture_config(
                {
                    .extent = {0, 1024},
                },
                VK_FORMAT_D32_SFLOAT);
        },
        "shadow depth config should reject zero width");
    require_throws(
        [] {
            (void)cubey::render::shadow_map_depth_texture_config({}, VK_FORMAT_UNDEFINED);
        },
        "shadow depth config should reject undefined depth format");
}

void test_shadow_depth_pass_info_declares_depth_only_state() {
    const VkPushConstantRange range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(float) * 16U,
    };
    const cubey::render::MaterialPassInfo pass = cubey::render::shadow_depth_pass_info({
        .label = "test.shadow",
        .push_constants = std::span<const VkPushConstantRange>{&range, 1},
        .cull_mode = VK_CULL_MODE_BACK_BIT,
    });

    require(pass.label == "test.shadow", "shadow pass should preserve label");
    require(pass.kind == cubey::render::MaterialPassKind::DepthOnly,
            "shadow pass should be depth-only");
    require(pass.descriptor_sets.empty(), "shadow pass should not require descriptors");
    require(pass.push_constants.size() == 1, "shadow pass should preserve push constants");
    require(pass.depth_test, "shadow pass should enable depth testing");
    require(pass.depth_write, "shadow pass should enable depth writes");
    require(pass.depth_compare_op == VK_COMPARE_OP_LESS,
            "shadow pass should default to less depth compare");
    require(pass.cull_mode == VK_CULL_MODE_BACK_BIT, "shadow pass should preserve cull mode");
}
