#include <cubey/render/material.h>
#include <cubey/render/pbr.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_pbr_vertex_layout_matches_shader_contract() {
    const cubey::render::VertexInputLayout layout = cubey::render::pbr_vertex_input_layout();
    require(layout.bindings().size() == 1, "PBR vertex layout should expose one binding");
    require(layout.bindings()[0].stride == sizeof(cubey::render::PbrVertex),
            "PBR vertex stride should match vertex type");
    require(layout.attributes.size() == 4, "PBR vertex layout should expose four attributes");
    require(layout.attributes[0].location == 0 &&
                layout.attributes[0].offset == offsetof(cubey::render::PbrVertex, position),
            "PBR position attribute should match shader location 0");
    require(layout.attributes[1].location == 1 &&
                layout.attributes[1].offset == offsetof(cubey::render::PbrVertex, normal),
            "PBR normal attribute should match shader location 1");
    require(layout.attributes[2].location == 2 &&
                layout.attributes[2].offset == offsetof(cubey::render::PbrVertex, tangent),
            "PBR tangent attribute should match shader location 2");
    require(layout.attributes[3].location == 3 &&
                layout.attributes[3].offset == offsetof(cubey::render::PbrVertex, uv0),
            "PBR UV0 attribute should match shader location 3");
}

void test_pbr_forward_pass_declares_scene_and_material_sets() {
    const cubey::render::MaterialPassInfo pass = cubey::render::pbr_forward_pass_info();
    require(pass.label == "pbr.forward", "PBR pass should use stable label");
    require(pass.kind == cubey::render::MaterialPassKind::ForwardColor,
            "PBR pass should be forward color");
    require(pass.depth_test && pass.depth_write, "PBR pass should enable depth");
    require(pass.descriptor_sets.size() == 2, "PBR pass should declare scene and material sets");
    require(pass.descriptor_sets[0].set == 0, "PBR scene descriptors should use set 0");
    require(pass.descriptor_sets[0].bindings.size() == 2,
            "PBR scene descriptors should include uniform and shadow map");
    require(pass.descriptor_sets[1].set == 1, "PBR material descriptors should use set 1");
    require(pass.descriptor_sets[1].bindings.size() == 5,
            "PBR material descriptors should include five texture slots");
    require(pass.push_constants.size() == 1, "PBR pass should declare push constants");
    require(pass.push_constants[0].size == sizeof(cubey::render::PbrPushConstants),
            "PBR push constant range should match struct size");

    const cubey::render::MaterialPassInfo alpha_pass =
        cubey::render::pbr_forward_pass_info(cubey::render::PbrForwardPassConfig{
            .blend = cubey::render::MaterialBlendMode::AlphaBlend,
        });
    require(alpha_pass.label == "pbr.forward.alpha",
            "PBR alpha pass should use a distinct label");
    require(alpha_pass.depth_test && !alpha_pass.depth_write,
            "PBR alpha pass should test but not write depth");
    require(alpha_pass.blend_enable, "PBR alpha pass should enable color blending");
    require(alpha_pass.src_color_blend_factor == VK_BLEND_FACTOR_SRC_ALPHA,
            "PBR alpha pass should source blend from alpha");
    require(alpha_pass.dst_color_blend_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "PBR alpha pass should destination blend from inverse alpha");
}
