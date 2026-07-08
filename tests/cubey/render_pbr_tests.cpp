#include "source_file_test_helpers.h"

#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/material.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pbr_material_resources.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>

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

using cubey::tests::read_source_file;
using cubey::tests::require_contains;
using cubey::tests::require_not_contains;

} // namespace

void test_pbr_vertex_layout_matches_shader_contract() {
    const cubey::render::VertexInputLayout layout = cubey::render::pbr_vertex_input_layout();
    require(layout.bindings().size() == 1, "PBR vertex layout should expose one binding");
    require(layout.bindings()[0].stride == sizeof(cubey::render::PbrVertex),
            "PBR vertex stride should match vertex type");
    require(layout.attributes.size() == 6, "PBR vertex layout should expose six attributes");
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
    require(layout.attributes[4].location == 4 &&
                layout.attributes[4].offset == offsetof(cubey::render::PbrVertex, uv1),
            "PBR UV1 attribute should match shader location 4");
    require(layout.attributes[5].location == 5 &&
                layout.attributes[5].offset == offsetof(cubey::render::PbrVertex, color0),
            "PBR COLOR0 attribute should match shader location 5");
}

void test_pbr_debug_view_names_parse_and_cycle() {
    require(cubey::render::pbr_debug_view_name(cubey::render::PbrDebugView::Final) == "final",
            "PBR final debug view should have a stable CLI name");
    require(cubey::render::pbr_debug_view_name(cubey::render::PbrDebugView::BaseColor) ==
                "base-color",
            "PBR base-color debug view should use kebab-case");
    require(cubey::render::pbr_debug_view_name(cubey::render::PbrDebugView::GeometricNormal) ==
                "geometric-normal",
            "PBR geometric normal debug view should use kebab-case");
    require(cubey::render::pbr_debug_view_from_name("") == cubey::render::PbrDebugView::Final,
            "empty PBR debug view name should mean final shading");
    require(cubey::render::pbr_debug_view_from_name("normal") ==
                cubey::render::PbrDebugView::Normal,
            "PBR debug parser should accept normal");
    require(cubey::render::pbr_debug_view_from_name("uv0") == cubey::render::PbrDebugView::Uv0,
            "PBR debug parser should accept uv0");
    require(cubey::render::next_pbr_debug_view(cubey::render::PbrDebugView::Final) ==
                cubey::render::PbrDebugView::BaseColor,
            "PBR debug cycling should start from base color after final");
    require(cubey::render::next_pbr_debug_view(cubey::render::PbrDebugView::Uv0) ==
                cubey::render::PbrDebugView::Final,
            "PBR debug cycling should wrap from uv0 to final");
    require_throws([] { (void)cubey::render::pbr_debug_view_from_name("not-a-view"); },
                   "PBR debug parser should reject unknown view names");
}

void test_pbr_forward_pass_declares_scene_and_material_sets() {
    const cubey::render::MaterialPassInfo pass = cubey::render::pbr_forward_pass_info();
    require(pass.label == "pbr.forward", "PBR pass should use stable label");
    require(pass.kind == cubey::render::MaterialPassKind::ForwardColor,
            "PBR pass should be forward color");
    require(pass.depth_test && pass.depth_write, "PBR pass should enable depth");
    require(pass.cull_mode == VK_CULL_MODE_BACK_BIT,
            "PBR forward pass should cull back faces by default");
    require(pass.descriptor_sets.size() == 2, "PBR pass should declare scene and material sets");
    require(pass.descriptor_sets[0].set == 0, "PBR scene descriptors should use set 0");
    require(pass.descriptor_sets[0].bindings.size() == 5,
            "PBR scene descriptors should include uniform, shadow map, and IBL textures");
    require(pass.descriptor_sets[1].set == 1, "PBR material descriptors should use set 1");
    require(pass.descriptor_sets[1].bindings.size() == 16,
            "PBR material descriptors should include base and extension textures plus uniforms");
    require(pass.descriptor_sets[1].bindings[5].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Specular),
            "PBR specular texture should use binding 5");
    require(pass.descriptor_sets[1].bindings[6].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::SpecularColor),
            "PBR specular color texture should use binding 6");
    require(pass.descriptor_sets[1].bindings[7].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Uniforms),
            "PBR material uniforms should use the final material descriptor binding");
    require(pass.descriptor_sets[1].bindings[5].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "PBR specular texture should be sampled");
    require(pass.descriptor_sets[1].bindings[6].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "PBR specular color texture should be sampled");
    require(pass.descriptor_sets[1].bindings[7].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "PBR material uniforms should be a uniform buffer");
    require(pass.descriptor_sets[1].bindings[8].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Clearcoat),
            "PBR clearcoat texture should use binding 8");
    require(pass.descriptor_sets[1].bindings[9].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::ClearcoatRoughness),
            "PBR clearcoat roughness texture should use binding 9");
    require(pass.descriptor_sets[1].bindings[10].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::ClearcoatNormal),
            "PBR clearcoat normal texture should use binding 10");
    require(pass.descriptor_sets[1].bindings[11].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::SheenColor),
            "PBR sheen color texture should use binding 11");
    require(pass.descriptor_sets[1].bindings[12].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::SheenRoughness),
            "PBR sheen roughness texture should use binding 12");
    require(pass.descriptor_sets[1].bindings[13].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Anisotropy),
            "PBR anisotropy texture should use binding 13");
    require(pass.descriptor_sets[1].bindings[14].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Iridescence),
            "PBR iridescence texture should use binding 14");
    require(pass.descriptor_sets[1].bindings[15].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::IridescenceThickness),
            "PBR iridescence thickness texture should use binding 15");
    for (std::size_t i = 8; i < pass.descriptor_sets[1].bindings.size(); ++i) {
        require(pass.descriptor_sets[1].bindings[i].type ==
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                "PBR extension textures should be sampled");
    }
    require(pass.push_constants.size() == 1, "PBR pass should declare push constants");
    require(pass.push_constants[0].size == sizeof(cubey::render::PbrPushConstants),
            "PBR push constant range should match struct size");

    const cubey::render::MaterialPassInfo alpha_pass =
        cubey::render::pbr_forward_pass_info(cubey::render::PbrForwardPassConfig{
            .blend = cubey::render::MaterialBlendMode::AlphaBlend,
        });
    require(alpha_pass.label == "pbr.forward.alpha", "PBR alpha pass should use a distinct label");
    require(alpha_pass.depth_test && !alpha_pass.depth_write,
            "PBR alpha pass should test but not write depth");
    require(alpha_pass.cull_mode == VK_CULL_MODE_BACK_BIT,
            "PBR alpha pass should keep the default single-sided cull policy");
    require(alpha_pass.blend_enable, "PBR alpha pass should enable color blending");
    require(alpha_pass.src_color_blend_factor == VK_BLEND_FACTOR_ONE,
            "PBR alpha pass should use premultiplied source color");
    require(alpha_pass.dst_color_blend_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "PBR alpha pass should destination blend from inverse alpha");
    require(alpha_pass.src_alpha_blend_factor == VK_BLEND_FACTOR_ONE,
            "PBR alpha pass should preserve source alpha");
    require(alpha_pass.dst_alpha_blend_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "PBR alpha pass should composite alpha with inverse source alpha");

    const cubey::render::MaterialPassInfo double_sided_pass =
        cubey::render::pbr_forward_pass_info(cubey::render::PbrForwardPassConfig{
            .cull_mode = VK_CULL_MODE_NONE,
        });
    require(double_sided_pass.cull_mode == VK_CULL_MODE_NONE,
            "PBR pass config should allow double-sided material pipelines");
}

void test_pbr_material_factors_are_uniforms_and_push_constants_are_model_only() {
    static_assert(sizeof(cubey::render::PbrPushConstants) == sizeof(cubey::math::Mat4));

    cubey::render::PbrMaterialFactors factors;
    factors.base_color_factor = {0.8F, 0.7F, 0.6F, 0.5F};
    factors.emissive_factor = {0.1F, 0.2F, 0.3F};
    factors.alpha_cutoff = 0.4F;
    factors.metallic_factor = 0.25F;
    factors.roughness_factor = 0.75F;
    factors.normal_scale = 0.9F;
    factors.occlusion_strength = 0.8F;
    factors.specular_color_factor = {0.7F, 0.8F, 0.9F};
    factors.specular_factor = 0.65F;
    factors.reflectance = 0.42F;
    factors.clearcoat_factor = 0.6F;
    factors.clearcoat_roughness_factor = 0.35F;
    factors.clearcoat_normal_scale = 0.8F;
    factors.sheen_color_factor = {0.2F, 0.3F, 0.4F};
    factors.sheen_roughness_factor = 0.45F;
    factors.anisotropy_strength = 0.55F;
    factors.anisotropy_rotation = 1.570796F;
    factors.iridescence_factor = 0.65F;
    factors.iridescence_ior = 1.4F;
    factors.iridescence_thickness_minimum = 120.0F;
    factors.iridescence_thickness_maximum = 520.0F;
    factors.alpha_mode = cubey::render::MaterialAlphaMode::Blend;
    factors.unlit = true;
    factors.texture_flags =
        cubey::render::pbr_material_texture_flag(cubey::render::PbrMaterialTextureFlag::Specular) |
        cubey::render::pbr_material_texture_flag(
            cubey::render::PbrMaterialTextureFlag::SpecularColor) |
        cubey::render::pbr_material_texture_flag(cubey::render::PbrMaterialTextureFlag::Clearcoat) |
        cubey::render::pbr_material_texture_flag(
            cubey::render::PbrMaterialTextureFlag::ClearcoatRoughness) |
        cubey::render::pbr_material_texture_flag(
            cubey::render::PbrMaterialTextureFlag::ClearcoatNormal) |
        cubey::render::pbr_material_texture_flag(
            cubey::render::PbrMaterialTextureFlag::SheenColor) |
        cubey::render::pbr_material_texture_flag(
            cubey::render::PbrMaterialTextureFlag::SheenRoughness) |
        cubey::render::pbr_material_texture_flag(
            cubey::render::PbrMaterialTextureFlag::Anisotropy) |
        cubey::render::pbr_material_texture_flag(
            cubey::render::PbrMaterialTextureFlag::Iridescence) |
        cubey::render::pbr_material_texture_flag(
            cubey::render::PbrMaterialTextureFlag::IridescenceThickness);
    factors.texture_transforms.base_color.offset_scale = {0.25F, 0.5F, 2.0F, 3.0F};
    factors.texture_transforms.base_color.rotation_texcoord = {0.0F, 1.0F, 1.0F, 0.0F};
    factors.texture_transforms.normal.offset_scale = {0.1F, 0.2F, 0.5F, 0.75F};
    factors.texture_transforms.normal.rotation_texcoord = {1.0F, 0.0F, 0.0F, 0.0F};
    factors.texture_transforms.clearcoat.offset_scale = {0.3F, 0.4F, 0.5F, 0.6F};
    factors.texture_transforms.sheen_color.rotation_texcoord = {0.0F, 1.0F, 0.0F, 0.0F};
    factors.texture_transforms.iridescence_thickness.offset_scale = {0.7F, 0.8F, 0.9F, 1.0F};

    const cubey::render::PbrMaterialUniforms uniforms =
        cubey::render::pbr_material_uniforms(factors);
    require(uniforms.base_color_factor == factors.base_color_factor,
            "PBR material uniforms should preserve base color factor");
    require(uniforms.emissive_alpha_cutoff.x == factors.emissive_factor.x &&
                uniforms.emissive_alpha_cutoff.y == factors.emissive_factor.y &&
                uniforms.emissive_alpha_cutoff.z == factors.emissive_factor.z &&
                uniforms.emissive_alpha_cutoff.w == factors.alpha_cutoff,
            "PBR material uniforms should pack emissive and alpha cutoff");
    require(uniforms.metallic_roughness_normal_occlusion.x == factors.metallic_factor &&
                uniforms.metallic_roughness_normal_occlusion.y == factors.roughness_factor &&
                uniforms.metallic_roughness_normal_occlusion.z == factors.normal_scale &&
                uniforms.metallic_roughness_normal_occlusion.w == factors.occlusion_strength,
            "PBR material uniforms should pack metallic, roughness, normal scale, and AO");
    require(uniforms.specular_color_factor.r == factors.specular_color_factor.r &&
                uniforms.specular_color_factor.g == factors.specular_color_factor.g &&
                uniforms.specular_color_factor.b == factors.specular_color_factor.b &&
                uniforms.specular_color_factor.a == factors.specular_factor,
            "PBR material uniforms should pack specular extension factors");
    require(uniforms.material_model.x == factors.reflectance,
            "PBR material uniforms should pack dielectric reflectance");
    require(uniforms.material_model.y ==
                static_cast<float>(
                    static_cast<std::underlying_type_t<cubey::render::MaterialAlphaMode>>(
                        factors.alpha_mode)),
            "PBR material uniforms should pack alpha mode");
    require(uniforms.material_model.z == 1.0F, "PBR material uniforms should pack unlit flag");
    require(uniforms.material_model.w == static_cast<float>(factors.texture_flags),
            "PBR material uniforms should pack optional texture flags");
    require(uniforms.clearcoat_factor_roughness_normal.x == factors.clearcoat_factor &&
                uniforms.clearcoat_factor_roughness_normal.y ==
                    factors.clearcoat_roughness_factor &&
                uniforms.clearcoat_factor_roughness_normal.z == factors.clearcoat_normal_scale,
            "PBR material uniforms should pack clearcoat factors");
    require(uniforms.sheen_color_roughness.x == factors.sheen_color_factor.x &&
                uniforms.sheen_color_roughness.y == factors.sheen_color_factor.y &&
                uniforms.sheen_color_roughness.z == factors.sheen_color_factor.z &&
                uniforms.sheen_color_roughness.w == factors.sheen_roughness_factor,
            "PBR material uniforms should pack sheen factors");
    require(uniforms.anisotropy_iridescence.x == factors.anisotropy_strength,
            "PBR material uniforms should pack anisotropy strength");
    require(uniforms.anisotropy_iridescence.y < 0.0001F &&
                uniforms.anisotropy_iridescence.y > -0.0001F,
            "PBR material uniforms should pack anisotropy rotation cosine");
    require(uniforms.anisotropy_iridescence.z > 0.9999F,
            "PBR material uniforms should pack anisotropy rotation sine");
    require(uniforms.anisotropy_iridescence.w == factors.iridescence_factor,
            "PBR material uniforms should pack iridescence factor");
    require(uniforms.iridescence_ior_thickness.x == factors.iridescence_ior &&
                uniforms.iridescence_ior_thickness.y == factors.iridescence_thickness_minimum &&
                uniforms.iridescence_ior_thickness.z == factors.iridescence_thickness_maximum,
            "PBR material uniforms should pack iridescence IOR and thickness range");
    require(uniforms.texture_transforms.base_color.offset_scale ==
                factors.texture_transforms.base_color.offset_scale,
            "PBR material uniforms should pack base color texture offset and scale");
    require(uniforms.texture_transforms.base_color.rotation_texcoord ==
                factors.texture_transforms.base_color.rotation_texcoord,
            "PBR material uniforms should pack base color texture rotation and UV set");
    require(uniforms.texture_transforms.normal.offset_scale ==
                factors.texture_transforms.normal.offset_scale,
            "PBR material uniforms should pack normal texture offset and scale");
    require(uniforms.texture_transforms.normal.rotation_texcoord ==
                factors.texture_transforms.normal.rotation_texcoord,
            "PBR material uniforms should pack normal texture rotation and UV set");
    require(uniforms.texture_transforms.clearcoat.offset_scale ==
                factors.texture_transforms.clearcoat.offset_scale,
            "PBR material uniforms should pack clearcoat texture transform");
    require(uniforms.texture_transforms.sheen_color.rotation_texcoord ==
                factors.texture_transforms.sheen_color.rotation_texcoord,
            "PBR material uniforms should pack sheen texture transform");
    require(uniforms.texture_transforms.iridescence_thickness.offset_scale ==
                factors.texture_transforms.iridescence_thickness.offset_scale,
            "PBR material uniforms should pack iridescence texture transform");
    const cubey::render::PbrMaterialUniforms opaque_uniforms =
        cubey::render::pbr_material_uniforms(factors, cubey::render::MaterialAlphaMode::Opaque);
    require(opaque_uniforms.material_model.y == 0.0F,
            "PBR material uniforms should allow render policy to override factor alpha mode");

    const cubey::render::PbrPushConstants constants =
        cubey::render::pbr_push_constants(cubey::math::Mat4{1.0F});
    require(constants.model == cubey::math::Mat4{1.0F},
            "PBR push constants should carry only the model matrix");
}

void test_pbr_default_texture_specs_cover_all_sampled_material_bindings() {
    const std::span<const cubey::render::PbrMaterialBinding> bindings =
        cubey::render::pbr_sampled_material_bindings();
    const std::span<const cubey::render::PbrDefaultTextureSpec> specs =
        cubey::render::pbr_default_texture_specs();

    require(bindings.size() == 15, "PBR should expose every sampled material binding");
    require(specs.size() == bindings.size(), "PBR default specs should cover every texture slot");
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        require(specs[i].binding == bindings[i],
                "PBR default texture specs should follow sampled binding order");
    }

    require(specs[0].binding == cubey::render::PbrMaterialBinding::BaseColor,
            "PBR default specs should start with base color");
    require(specs[0].format == VK_FORMAT_R8G8B8A8_SRGB,
            "base-color default should be sampled as sRGB");
    require(specs[0].rgba8 == std::array<std::uint8_t, 4>{255, 255, 255, 255},
            "base-color default should be opaque white");

    require(specs[1].binding == cubey::render::PbrMaterialBinding::MetallicRoughness,
            "PBR default specs should include metallic-roughness");
    require(specs[1].format == VK_FORMAT_R8G8B8A8_UNORM,
            "metallic-roughness default should be linear");
    require(specs[1].rgba8 == std::array<std::uint8_t, 4>{255, 255, 255, 255},
            "metallic-roughness default should preserve roughness and metallic at one");

    require(specs[2].binding == cubey::render::PbrMaterialBinding::Normal,
            "PBR default specs should include normal");
    require(specs[2].rgba8 == std::array<std::uint8_t, 4>{128, 128, 255, 255},
            "normal default should be the flat tangent-space normal");

    require(specs[4].binding == cubey::render::PbrMaterialBinding::Emissive,
            "PBR default specs should include emissive");
    require(specs[4].format == VK_FORMAT_R8G8B8A8_SRGB,
            "emissive default should be sampled as sRGB");
    require(specs[4].rgba8 == std::array<std::uint8_t, 4>{0, 0, 0, 255},
            "emissive default should be black");
}

void test_pbr_material_table_groups_factors_and_supports_lifetime_operations() {
    cubey::render::PbrMaterialTable table;
    const cubey::render::MaterialHandle material{.index = 4, .generation = 2};

    cubey::render::PbrMaterialFactors factors;
    factors.base_color_factor = {0.2F, 0.3F, 0.4F, 0.5F};
    table.set_factors(material, factors);

    require(table.contains_factors(material), "PBR material table should store material factors");
    require(table.factors(material).base_color_factor == factors.base_color_factor,
            "PBR material table should retrieve material factors by handle");

    table.erase(material);
    require(!table.contains_factors(material), "PBR material erase should remove factors");

    table.set_factors(material, factors);
    table.clear();
    require(!table.contains_factors(material), "PBR material clear should remove all factors");
}

void test_pbr_material_table_tracks_descriptor_layout_explicitly() {
    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string header =
        read_source_file(source_root / "include/cubey/render/pbr_material_resources.h");
    const std::string source =
        read_source_file(source_root / "src/cubey/render/pbr_material_resources.cpp");

    require_contains(header, "VkDescriptorSetLayout descriptor_set_layout_",
                     "PBR material table should store a table-level descriptor layout");
    require_contains(header, "register_descriptor_set_layout(instance.layout())",
                     "PBR material table should register inserted instance layouts");
    require_contains(source, "PbrMaterialTable::register_descriptor_set_layout",
                     "PBR material table should centralize descriptor layout registration");
    require_contains(source, "instance requires a descriptor set layout",
                     "PBR material table should reject null descriptor layouts");
    require_contains(source, "return descriptor_set_layout_;",
                     "PBR material table descriptor layout should return the stored layout");
    require_not_contains(
        source, "descriptor_set_layout_ != layout",
        "PBR material table should allow equivalent layouts with distinct handles");
    require_not_contains(source, "instances_.first().layout()",
                         "PBR material table descriptor layout should not depend on map order");
}

void test_pbr_scene_uniforms_carry_display_transform() {
    const cubey::render::PbrDisplayTransform display{
        .exposure = 1.25F,
        .tonemap = cubey::render::PbrTonemap::Aces,
        .output_encoding = cubey::render::PbrOutputEncoding::Srgb,
    };

    const cubey::math::Vec4 uniform = cubey::render::pbr_display_transform_uniform(display);
    require(uniform.x == display.exposure, "PBR display transform should pack exposure stops");
    require(uniform.y == 1.0F, "PBR display transform should pack ACES tonemap mode");
    require(uniform.z == 1.0F, "PBR display transform should pack sRGB output encoding");

    const cubey::render::PbrSceneUniforms scene_uniforms{
        .display_transform = uniform,
    };
    require(scene_uniforms.display_transform == uniform,
            "PBR scene uniforms should carry final display transform controls");

    const cubey::render::PbrDisplayTransform unorm_transform =
        cubey::render::pbr_display_transform_for_target(VK_FORMAT_R8G8B8A8_UNORM);
    const cubey::render::PbrDisplayTransform srgb_transform =
        cubey::render::pbr_display_transform_for_target(VK_FORMAT_B8G8R8A8_SRGB);
    require(unorm_transform.output_encoding == cubey::render::PbrOutputEncoding::Srgb,
            "UNORM final targets should request shader-side sRGB output encoding");
    require(srgb_transform.output_encoding == cubey::render::PbrOutputEncoding::Linear,
            "sRGB final targets should leave output encoding to the attachment");
}

void test_pbr_post_pass_declares_uniforms_and_scene_color() {
    const cubey::render::MaterialPassInfo pass = cubey::render::pbr_post_pass_info();
    require(pass.label == "pbr.post", "PBR post pass should use a stable label");
    require(pass.kind == cubey::render::MaterialPassKind::ForwardColor,
            "PBR post pass should be a forward color pass");
    require(!pass.depth_test && !pass.depth_write, "PBR post pass should not use depth");
    require(pass.descriptor_sets.size() == 1, "PBR post pass should declare one set");
    require(pass.descriptor_sets[0].set == 0, "PBR post descriptors should use set 0");
    require(pass.descriptor_sets[0].bindings.size() == 2,
            "PBR post pass should declare uniforms and scene color");
    require(pass.descriptor_sets[0].bindings[0].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrPostBinding::PostUniforms),
            "PBR post uniforms should use binding 0");
    require(pass.descriptor_sets[0].bindings[0].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "PBR post uniforms should be a uniform buffer");
    require(pass.descriptor_sets[0].bindings[0].stage_flags == VK_SHADER_STAGE_FRAGMENT_BIT,
            "PBR post uniforms should be fragment-only");
    require(pass.descriptor_sets[0].bindings[1].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrPostBinding::SceneColor),
            "PBR post scene color should use binding 1");
    require(pass.descriptor_sets[0].bindings[1].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "PBR post scene color should be sampled");
    require(pass.push_constants.empty(), "PBR post pass should not use push constants");

    const cubey::render::PbrPostUniforms uniforms{
        .display_transform = {1.0F, 1.0F, 0.0F, 0.0F},
    };
    require(uniforms.display_transform == cubey::math::Vec4{1.0F, 1.0F, 0.0F, 0.0F},
            "PBR post uniforms should carry final display transform controls");
}

void test_hdr_post_frame_helpers_pack_scene_color_and_display_transform() {
    const cubey::render::RenderGraphTextureDesc desc = cubey::render::hdr_scene_color_texture_desc(
        "test scene color", {640U, 360U}, VK_FORMAT_R16G16B16A16_SFLOAT);
    require(desc.label == "test scene color", "HDR post scene color should keep caller labels");
    require(desc.extent.width == 640U && desc.extent.height == 360U && desc.extent.depth == 1U,
            "HDR post scene color should convert 2D target extents to graph texture extents");
    require(desc.format == VK_FORMAT_R16G16B16A16_SFLOAT,
            "HDR post scene color should preserve the requested format");
    require(desc.aspects == VK_IMAGE_ASPECT_COLOR_BIT,
            "HDR post scene color should declare color aspect usage");

    const cubey::render::PbrPostUniforms uniforms = cubey::render::hdr_post_uniforms(
        VK_FORMAT_B8G8R8A8_SRGB, 0.75F, cubey::render::PbrTonemap::Linear);
    const cubey::render::PbrDisplayTransform display_transform =
        cubey::render::pbr_display_transform_for_target(VK_FORMAT_B8G8R8A8_SRGB, 0.75F,
                                                        cubey::render::PbrTonemap::Linear);
    require(uniforms.display_transform ==
                cubey::render::pbr_display_transform_uniform(display_transform),
            "HDR post uniforms should reuse the canonical PBR display transform packing");
}

void test_pbr_skybox_uniforms_are_uniform_buffer_safe() {
    static_assert(std::is_trivially_copyable_v<cubey::render::PbrSkyboxUniforms>);
    static_assert(sizeof(cubey::render::PbrSkyboxUniforms) ==
                  (sizeof(cubey::math::Mat4) + (sizeof(cubey::math::Vec4) * 3U)));

    const cubey::render::PbrSkyboxUniforms uniforms{
        .inverse_view_projection = cubey::math::Mat4{1.0F},
        .camera_position = {1.0F, 2.0F, 3.0F, 1.0F},
        .environment_rotation_intensity = {0.0F, 1.0F, 2.0F, 0.0F},
        .display_transform = {0.5F, 1.0F, 0.0F, 0.0F},
    };

    require(uniforms.camera_position.x == 1.0F && uniforms.camera_position.y == 2.0F &&
                uniforms.camera_position.z == 3.0F && uniforms.camera_position.w == 1.0F,
            "PBR skybox uniforms should carry the camera world position");
    require(uniforms.environment_rotation_intensity.x == 0.0F &&
                uniforms.environment_rotation_intensity.y == 1.0F &&
                uniforms.environment_rotation_intensity.z == 2.0F,
            "PBR skybox uniforms should carry environment rotation and intensity");
    require(uniforms.display_transform.x == 0.5F && uniforms.display_transform.y == 1.0F,
            "PBR skybox uniforms should carry display transform controls");
}

void test_pbr_skybox_pass_declares_scene_set() {
    const cubey::render::MaterialPassInfo pass = cubey::render::pbr_skybox_pass_info();
    require(pass.label == "pbr.skybox", "PBR skybox pass should use stable label");
    require(pass.kind == cubey::render::MaterialPassKind::ForwardColor,
            "PBR skybox pass should be forward color");
    require(!pass.depth_test && !pass.depth_write, "PBR skybox pass should not use depth");
    require(pass.descriptor_sets.size() == 1, "PBR skybox pass should declare one set");
    require(pass.descriptor_sets[0].set == 0, "PBR skybox descriptors should use set 0");
    require(pass.descriptor_sets[0].bindings.size() == 2,
            "PBR skybox pass should declare uniforms and environment cube");
    require(pass.descriptor_sets[0].bindings[0].binding == 0,
            "PBR skybox uniforms should use binding 0");
    require(pass.descriptor_sets[0].bindings[0].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "PBR skybox uniforms should be a uniform buffer");
    require(pass.descriptor_sets[0].bindings[0].stage_flags ==
                (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            "PBR skybox uniforms should be visible to vertex and fragment shaders");
    require(pass.descriptor_sets[0].bindings[1].binding == 1,
            "PBR skybox environment should use binding 1");
    require(pass.descriptor_sets[0].bindings[1].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "PBR skybox environment should be a sampled image");
    require(pass.descriptor_sets[0].bindings[1].stage_flags == VK_SHADER_STAGE_FRAGMENT_BIT,
            "PBR skybox environment should be fragment-only");
    require(pass.push_constants.empty(), "PBR skybox pass should not use push constants");
}

void test_pbr_reflectance_helpers_match_filament_convention() {
    require(cubey::render::pbr_f0_from_reflectance(0.5F) == 0.04F,
            "reflectance 0.5 should map to dielectric F0 0.04");
    require(cubey::render::pbr_reflectance_from_ior(1.5F) == 0.5F,
            "IOR 1.5 should map to default reflectance 0.5");
    require(cubey::render::pbr_f0_from_reflectance(0.0F) == 0.0F,
            "minimum reflectance should map to zero F0");
    require(cubey::render::pbr_f0_from_reflectance(1.0F) == 0.16F,
            "maximum reflectance should map to F0 0.16");
}

void test_pbr_shaders_use_filament_style_material_remap() {
    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string pbr = read_source_file(source_root / "shaders/cubey/pbr.glsl");
    const std::string post =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr_post.frag");
    const std::string furnace =
        read_source_file(source_root / "projects/pbr_furnace/shaders/pbr_furnace.frag");
    const std::string gltf =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr.frag");
    const std::string gltf_vertex =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr.vert");
    const std::string forward_pbr_skybox =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr_skybox.frag");
    const std::string gltf_shadow =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr_shadow_depth.frag");

    require_contains(pbr, "cubey_pbr_diffuse_color",
                     "PBR shader should expose a baseColor-to-diffuse remap helper");
    require_contains(pbr, "cubey_pbr_f0",
                     "PBR shader should expose a baseColor-to-F0 remap helper");
    require_contains(pbr, "cubey_pbr_f0_from_reflectance",
                     "PBR shader should expose the Filament reflectance-to-F0 helper");
    require_contains(pbr, "cubey_pbr_dielectric_f0",
                     "PBR shader should expose dielectric F0 material extension helper");
    require_contains(pbr, "cubey_pbr_lambert_diffuse",
                     "PBR shader should expose a Lambert diffuse helper");
    require_contains(pbr, "cubey_pbr_apply_display_transform",
                     "PBR shader should expose a final display transform helper");
    require_contains(post, "cubey_pbr_apply_display_transform(color, post.display_transform)",
                     "PBR post shader should apply display transform to the HDR scene color");
    require_contains(post, "uniform sampler2D scene_color",
                     "PBR post shader should sample the scene color texture");

    require_contains(furnace, "vec4 display_transform",
                     "PBR furnace should keep direct display transform controls");
    require_contains(furnace, "cubey_pbr_apply_display_transform(color, scene.display_transform)",
                     "PBR furnace should keep direct display transform output");

    require_not_contains(gltf, "cubey_pbr_apply_display_transform",
                         "glTF PBR shader should leave display transform to the post pass");
    require_not_contains(forward_pbr_skybox, "cubey_pbr_apply_display_transform",
                         "glTF skybox shader should leave display transform to the post pass");

    for (const std::string* shader : {&furnace, &gltf}) {
        require_contains(*shader, "uniform PbrMaterialUniforms",
                         "PBR fragment shaders should read per-material uniforms");
        require_not_contains(
            *shader, "push_constants.base_color_factor",
            "PBR fragment shaders should not read material factors from push constants");
        require_not_contains(
            *shader, "push_constants.metallic_roughness_normal_occlusion",
            "PBR fragment shaders should not read material factors from push constants");
        require_contains(*shader, "vec3 diffuse_color = cubey_pbr_diffuse_color(albedo, metallic);",
                         "PBR fragment shaders should compute diffuseColor explicitly");
        require_contains(*shader, "cubey_pbr_dielectric_f0",
                         "PBR fragment shaders should compute dielectric F0 from material factors");
        require_contains(*shader, "material.material_model.x",
                         "PBR fragment shaders should read material reflectance");
        require_contains(*shader, "cubey_pbr_has_material_texture",
                         "PBR fragment shaders should branch optional texture reads by flag");
        require_contains(*shader, "vec3 f0 = cubey_pbr_f0(albedo, metallic, dielectric_f0);",
                         "PBR fragment shaders should compute F0 through the shared helper");
        require_contains(
            *shader, "irradiance * diffuse_color",
            "PBR indirect diffuse should use diffuseColor without Fresnel attenuation");
        require_not_contains(*shader, "(vec3(1.0) - ibl_f) * (1.0 - metallic)",
                             "PBR indirect diffuse should not double-attenuate metallic values");
        require_not_contains(*shader, "(vec3(1.0) - f) * (1.0 - metallic)",
                             "PBR direct diffuse should not double-attenuate metallic values");
    }

    require_contains(gltf, "cubey_pbr_lambert_diffuse(diffuse_color)",
                     "glTF direct diffuse should use the shared Lambert helper");
    require_contains(gltf,
                     "float output_alpha = material.material_model.y > 1.5 ? "
                     "base_color.a : 1.0;",
                     "glTF PBR shader should only use base alpha for blended materials");
    require_contains(gltf, "material.material_model.z > 0.5",
                     "glTF PBR shader should branch for unlit materials");
    require_contains(gltf, "out_color = vec4(color * output_alpha, output_alpha);",
                     "glTF PBR shader should emit premultiplied alpha for blending");
    require_contains(gltf, "if (alpha_cutoff > 0.0 && base_color.a < alpha_cutoff)",
                     "glTF PBR shader should discard masked fragments by alpha cutoff");
    require_contains(gltf, "layout(location = 5) in vec2 frag_uv1",
                     "glTF PBR shader should receive the second UV set");
    require_contains(gltf, "layout(location = 6) in vec4 frag_color0",
                     "glTF PBR shader should receive vertex colors");
    require_contains(gltf, "vec2 cubey_pbr_transformed_uv",
                     "glTF PBR shader should transform texture coordinates per slot");
    require_contains(gltf, "base_color *= frag_color0",
                     "glTF PBR shader should multiply vertex color into base color");
    require_contains(gltf, "texture(base_color_texture, cubey_pbr_transformed_uv",
                     "glTF PBR shader should sample base color through transformed UVs");
    require_contains(gltf, "uniform sampler2D specular_texture",
                     "glTF PBR shader should bind KHR_materials_specular strength texture");
    require_contains(gltf, "uniform sampler2D specular_color_texture",
                     "glTF PBR shader should bind KHR_materials_specular color texture");
    require_contains(gltf, "uniform sampler2D clearcoat_texture",
                     "glTF PBR shader should bind KHR_materials_clearcoat texture");
    require_contains(gltf, "uniform sampler2D clearcoat_roughness_texture",
                     "glTF PBR shader should bind KHR_materials_clearcoat roughness texture");
    require_contains(gltf, "uniform sampler2D clearcoat_normal_texture",
                     "glTF PBR shader should bind KHR_materials_clearcoat normal texture");
    require_contains(gltf, "uniform sampler2D sheen_color_texture",
                     "glTF PBR shader should bind KHR_materials_sheen color texture");
    require_contains(gltf, "uniform sampler2D sheen_roughness_texture",
                     "glTF PBR shader should bind KHR_materials_sheen roughness texture");
    require_contains(gltf, "uniform sampler2D anisotropy_texture",
                     "glTF PBR shader should bind KHR_materials_anisotropy texture");
    require_contains(gltf, "uniform sampler2D iridescence_texture",
                     "glTF PBR shader should bind KHR_materials_iridescence texture");
    require_contains(gltf, "uniform sampler2D iridescence_thickness_texture",
                     "glTF PBR shader should bind KHR_materials_iridescence thickness texture");
    require_contains(gltf, "cubey_pbr_transformed_uv(material.specular_transform)",
                     "glTF PBR shader should sample specular strength through transformed UVs");
    require_contains(gltf, "cubey_pbr_transformed_uv(material.specular_color_transform)",
                     "glTF PBR shader should sample specular color through transformed UVs");
    require_contains(gltf, "cubey_pbr_clearcoat_direct",
                     "glTF PBR shader should evaluate clearcoat direct lighting");
    require_contains(gltf, "cubey_pbr_sheen_direct",
                     "glTF PBR shader should evaluate sheen direct lighting");
    require_contains(gltf, "cubey_pbr_distribution_ggx_anisotropic",
                     "glTF PBR shader should evaluate anisotropic specular");
    require_contains(gltf, "cubey_pbr_iridescence_f0",
                     "glTF PBR shader should evaluate iridescence Fresnel tint");
    require_contains(gltf_vertex, "orthogonalizeTangent",
                     "glTF PBR vertex shader should re-orthogonalize normal-map tangent frames");
    require_contains(gltf_vertex, "mat3(model) * in_tangent.xyz",
                     "glTF PBR vertex shader should transform tangents with the model linear part");
    require_contains(gltf, "if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SPECULAR))",
                     "glTF PBR shader should skip specular strength texture when absent");
    require_contains(gltf, "if (cubey_pbr_has_material_texture(CUBEY_PBR_TEXTURE_SPECULAR_COLOR))",
                     "glTF PBR shader should skip specular color texture when absent");
    require_contains(gltf_shadow, "uniform sampler2D base_color_texture",
                     "glTF shadow mask shader should sample base color alpha");
    require_contains(gltf_shadow, "uniform PbrMaterialUniforms",
                     "glTF shadow mask shader should read per-material alpha cutoff");
    require_contains(gltf_shadow, "cubey_pbr_transformed_uv",
                     "glTF shadow mask shader should apply base-color texture transform");
    require_contains(gltf_shadow, "discard",
                     "glTF shadow mask shader should discard fragments below alpha cutoff");
}

void test_forward_pbr_shader_package_uses_renderer_names() {
    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string shader_cmake = read_source_file(source_root / "cmake/CubeyShaders.cmake");
    const std::string viewer_cmake =
        read_source_file(source_root / "projects/gltf_viewer/CMakeLists.txt");
    const std::string material_cubes_cmake =
        read_source_file(source_root / "examples/material_cubes/CMakeLists.txt");

    require_contains(shader_cmake, "cubey_forward_pbr_shader_sources",
                     "CMake should expose the shared forward PBR shader package");
    require_contains(shader_cmake, "cubey_forward_pbr_shader_depends",
                     "CMake should expose the shared forward PBR shader dependencies");
    require_contains(shader_cmake, "cubey_shared_shader_depends",
                     "CMake should expose shared shader dependencies");
    require_contains(shader_cmake, "cubey_atmosphere_shader_depends",
                     "CMake should expose shared atmosphere shader dependencies");
    require_contains(shader_cmake, "forward_pbr_atmosphere_shader_depends",
                     "forward PBR package should track atmosphere shader dependencies");
    require_contains(shader_cmake, "forward_pbr_shadow_depth.frag",
                     "shared forward PBR package should include the shadow mask shader");
    require_contains(shader_cmake, "shaders/cubey/atmosphere/atmosphere.frag",
                     "shared forward PBR package should include the atmosphere background shader");
    require_contains(shader_cmake, "atmosphere_reflection_prefilter.frag",
                     "shared forward PBR package should include the atmosphere probe prefilter");
    require_not_contains(
        shader_cmake, "atmosphere_reflection_irradiance.frag",
        "shared forward PBR package should not include removed atmosphere irradiance path");
    require_contains(viewer_cmake, "cubey_forward_pbr_shader_sources",
                     "glTF viewer should consume the shared forward PBR shader package");
    require_contains(viewer_cmake, "cubey_forward_pbr_shader_depends",
                     "glTF viewer should consume the shared forward PBR shader dependencies");
    require_contains(material_cubes_cmake, "cubey_forward_pbr_shader_sources",
                     "material_cubes should consume the shared forward PBR shader package");
    require_contains(material_cubes_cmake, "cubey_forward_pbr_shader_depends",
                     "material_cubes should consume the shared forward PBR shader dependencies");
    require_not_contains(shader_cmake, "gltf_pbr",
                         "shared forward PBR package should not use old glTF shader names");

    std::size_t shader_count = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(source_root / "shaders/cubey/forward_pbr")) {
        if (!entry.is_regular_file()) {
            continue;
        }
        ++shader_count;
        require_not_contains(entry.path().filename().string(), "gltf_",
                             "shared forward PBR shader filenames should be renderer-named");
    }
    require(shader_count == 8, "shared forward PBR package should contain eight shader files");
}

void test_gltf_viewer_sample_asset_smoke_tests_cover_material_and_tangent_cases() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string cmake = read_source_file(source_root / "projects/gltf_viewer/CMakeLists.txt");
    const std::string gltf_docs =
        read_source_file(source_root / "docs/architecture/gltf-assets.md");

    require_contains(cmake, "function(cubey_add_gltf_viewer_sample_smoke_test",
                     "glTF viewer sample smoke tests should use a helper");
    require_contains(cmake, "gltf_viewer_static_environment_headless_writes_png",
                     "glTF viewer smoke tests should cover the static PBR environment path");
    require_contains(cmake, "--pbr-environment-source",
                     "glTF viewer smoke tests should exercise explicit environment source wiring");
    require_contains(cmake, "if (NOT CUBEY_GLTF_SAMPLE_ASSETS_DIR)",
                     "glTF viewer sample smoke tests should stay optional");
    require_contains(cmake, "FATAL_ERROR",
                     "configured glTF sample asset directories should fail on missing samples");
    require_contains(cmake, "AlphaBlendModeTest/glTF/AlphaBlendModeTest.gltf",
                     "glTF viewer sample smoke tests should cover alpha blending");
    require_contains(cmake, "SpecularTest/glTF/SpecularTest.gltf",
                     "glTF viewer sample smoke tests should cover specular materials");
    require_contains(cmake, "TextureTransformTest/glTF/TextureTransformTest.gltf",
                     "glTF viewer sample smoke tests should cover texture transforms");
    require_contains(cmake, "TextureTransformMultiTest/glTF/TextureTransformMultiTest.gltf",
                     "glTF viewer sample smoke tests should cover multi-texture transforms");
    require_contains(cmake, "NormalTangentTest/glTF/NormalTangentTest.gltf",
                     "glTF viewer sample smoke tests should cover tangent-space normals");
    require_contains(cmake, "NormalTangentMirrorTest/glTF/NormalTangentMirrorTest.gltf",
                     "glTF viewer sample smoke tests should cover mirrored tangent spaces");
    require_contains(cmake, "DamagedHelmet/glTF/DamagedHelmet.gltf",
                     "glTF viewer sample smoke tests should cover the default PBR sample");
    require_contains(cmake, "AnisotropyBarnLamp/glTF-KTX-BasisU/AnisotropyBarnLamp.gltf",
                     "glTF viewer sample smoke tests should cover required KTX2 BasisU textures");
    require_contains(cmake, "StainedGlassLamp/glTF-KTX-BasisU/StainedGlassLamp.gltf",
                     "glTF viewer sample smoke tests should cover KTX2 alpha/emissive textures");

    require_contains(gltf_docs, "MikkTSpace",
                     "glTF docs should record the tangent-space reference");
    require_contains(gltf_docs, "KHR_texture_basisu",
                     "glTF docs should record the KTX2 BasisU import path");
    require_contains(gltf_docs, "BC7",
                     "glTF docs should record the desktop compressed texture target");
    require_contains(gltf_docs, "current fallback tangent generator",
                     "glTF docs should keep the current tangent generator policy explicit");
    require_contains(gltf_docs, "not a trivial swap",
                     "glTF docs should capture why MikkTSpace is not added immediately");
    require_contains(gltf_docs, "NormalTangentTest",
                     "glTF docs should point tangent validation at Khronos sample assets");
}

void test_gltf_material_fallback_textures_preserve_pbr_factor_channels() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string resources =
        read_source_file(source_root / "src/cubey/render/pbr_material_resources.cpp");

    require_contains(resources, ".binding = PbrMaterialBinding::MetallicRoughness",
                     "shared PBR defaults should create a metallic-roughness fallback texture");
    require_contains(
        resources,
        ".binding = PbrMaterialBinding::MetallicRoughness,\n        .rgba8 = {255, 255, 255, "
        "255},\n        .format = VK_FORMAT_R8G8B8A8_UNORM",
        "metallic-roughness fallback should leave roughness and metallic channels at one");
}

void test_pbr_examples_and_gltf_importer_share_material_resources() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string importer_header =
        read_source_file(source_root / "include/cubey/engine/gltf_scene_importer.h");
    const std::string importer =
        read_source_file(source_root / "src/cubey/engine/gltf_scene_importer_materials.cpp");
    const std::string viewer_header =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_app_internal.h");
    const std::string viewer =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_assets.cpp");
    const std::string furnace_header =
        read_source_file(source_root / "projects/pbr_furnace/pbr_furnace_app_internal.h");
    const std::string furnace =
        read_source_file(source_root / "projects/pbr_furnace/pbr_furnace_resources.cpp");
    const std::string material_cubes =
        read_source_file(source_root / "examples/material_cubes/material_cubes_app_internal.h") +
        read_source_file(source_root / "examples/material_cubes/material_cubes_resources.cpp");

    require_contains(importer_header, "render::PbrMaterialTable materials",
                     "glTF import resources should expose a shared PBR material table");
    require_contains(importer_header, "std::optional<render::PbrDefaultTextureSet>",
                     "glTF import resources should own the shared PBR default texture set");
    require_contains(importer, "resources.materials.set_factors(",
                     "glTF importer should store factors through the PBR material table");
    require_contains(importer, "resources.materials.emplace_instance(",
                     "glTF importer should store instances through the PBR material table");
    require_contains(importer, "render::pbr_default_texture(",
                     "glTF importer should resolve missing textures through shared defaults");
    require_not_contains(importer_header, "material_factors",
                         "glTF import resources should not expose a parallel factor map");
    require_not_contains(importer_header, "base_color_default",
                         "glTF import resources should not expose per-slot default textures");

    require_contains(viewer, "import_resources_.default_textures",
                     "glTF viewer fallback should use the import resource default texture set");
    require_contains(viewer, "cubey::render::pbr_default_sampled_image_bindings(",
                     "glTF viewer fallback material should use shared default sampled bindings");
    require_not_contains(viewer_header, "base_color_default_",
                         "glTF viewer should not carry duplicated PBR default textures");

    require_contains(furnace_header, "render::PbrMaterialTable materials_",
                     "PBR furnace should group material factors and instances in one table");
    require_contains(furnace_header, "render::PbrDefaultTextureSet",
                     "PBR furnace should own one shared default texture set");
    require_contains(furnace, "cubey::render::pbr_default_sampled_image_bindings(",
                     "PBR furnace materials should use shared default sampled bindings");
    require_not_contains(furnace_header, "material_factors_",
                         "PBR furnace should not carry a parallel factor map");
    require_not_contains(furnace_header, "base_color_default_",
                         "PBR furnace should not carry duplicated PBR default textures");

    require_contains(material_cubes, "cubey::render::PbrMaterialTable materials_",
                     "material cubes should group material factors and instances in one table");
    require_contains(material_cubes, "cubey::render::PbrDefaultTextureSet",
                     "material cubes should own one shared default texture set");
    require_contains(material_cubes, "cubey::render::pbr_default_sampled_image_bindings(",
                     "material cubes should use shared default sampled bindings");
    require_not_contains(material_cubes, "material_factors_",
                         "material cubes should not carry a parallel factor map");
    require_not_contains(material_cubes, "base_color_default_",
                         "material cubes should not carry duplicated PBR default textures");
}

void test_pbr_consumers_use_atmosphere_lighting_foundation() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string gltf_header =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_app_internal.h");
    const std::string gltf_app =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_app.cpp");
    const std::string gltf_assets =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_assets.cpp");
    const std::string gltf_render =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_render.cpp");
    const std::string gltf_scene =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_scene.cpp");
    const std::string ocean_app = read_source_file(source_root / "projects/ocean/ocean_app.cpp");
    const std::string ocean_ui = read_source_file(source_root / "projects/ocean/ocean_ui.cpp");
    const std::string atmosphere_ui =
        read_source_file(source_root / "include/cubey/host/atmosphere_environment_ui.h") +
        read_source_file(source_root / "src/cubey/host/atmosphere_environment_ui.cpp");
    const std::string pbr_docs = read_source_file(source_root / "docs/architecture/pbr-ibl.md");

    require_contains(atmosphere_ui, "draw_atmosphere_environment_controls",
                     "shared atmosphere UI should expose reusable environment controls");
    require_contains(atmosphere_ui, "atmosphere_environment_resolve_run_state",
                     "shared atmosphere UI should resolve edited run state through engine helpers");
    require_contains(gltf_header, "AtmosphereEnvironmentRuntime atmosphere_runtime_",
                     "glTF viewer should own a shared atmosphere environment runtime");
    require_not_contains(gltf_header, "AtmosphereDiffuseSource",
                         "glTF viewer should not expose multiple atmosphere diffuse paths");
    require_contains(gltf_app, "gltf_viewer_atmosphere_run_state",
                     "glTF viewer should resolve atmosphere options from RunConfig");
    require_contains(gltf_app, "atmosphere_runtime_.set_environment",
                     "glTF viewer should feed atmosphere config into the shared runtime");
    require_contains(gltf_app, "callbacks.draw_ui",
                     "glTF viewer should expose a windowed control panel");
    require_contains(gltf_app, "draw_atmosphere_environment_controls",
                     "glTF viewer should consume the shared atmosphere UI controls");
    require_contains(gltf_app, "refresh_atmosphere_controls",
                     "glTF viewer should push atmosphere UI edits into runtime lighting");
    require_contains(
        gltf_app, ".reference_geometry_enabled = false",
        "glTF viewer should disable atmosphere reference geometry for PBR backgrounds");
    require_contains(gltf_scene, "primary_light_direction",
                     "glTF viewer should use atmosphere primary light for direct lighting");
    require_contains(gltf_scene, "atmosphere_runtime_.scene_environment()",
                     "glTF viewer should feed runtime diffuse environment into Environment3D");
    require_contains(gltf_assets, "atmosphere_background_textures()",
                     "glTF viewer should provide atmosphere background texture bindings");
    require_contains(gltf_assets, "atmosphere_runtime_.pbr_environment_bindings",
                     "glTF viewer should route PBR environment bindings through the runtime");
    require_contains(gltf_render, "ForwardPbrRenderer3DBackgroundMode::Atmosphere",
                     "glTF viewer should select the procedural atmosphere background");
    require_contains(gltf_render, "record_atmosphere_environment_if_needed",
                     "glTF viewer should update atmosphere runtime before the PBR pass");
    require_contains(gltf_scene, "atmosphere_background_uniforms",
                     "glTF viewer should compute procedural atmosphere background uniforms");
    require_contains(ocean_app, "atmosphere_environment_lighting(atmosphere_state_.environment)",
                     "ocean should derive its sun direction through shared atmosphere state");
    require_contains(ocean_app, "AtmosphereEnvironmentRuntime atmosphere_runtime_",
                     "ocean should own the shared atmosphere runtime for reflections");
    require_contains(ocean_ui, "draw_atmosphere_environment_controls",
                     "ocean should consume the shared atmosphere UI controls");
    require_not_contains(ocean_ui, "void draw_environment_controls",
                         "ocean should not keep a project-local copy of atmosphere controls");
    require_contains(pbr_docs, "runtime atmosphere reflection probe",
                     "PBR docs should capture the current atmosphere lighting boundary");
}

void test_pbr_diagnostics_are_exposed_in_gltf_viewer_and_material_cubes() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string gltf_viewer =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_app_internal.h") +
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_app.cpp") +
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_render.cpp");
    const std::string material_cubes =
        read_source_file(source_root / "examples/material_cubes/material_cubes_app_internal.h") +
        read_source_file(source_root / "examples/material_cubes/material_cubes_app.cpp") +
        read_source_file(source_root / "examples/material_cubes/material_cubes_render.cpp");

    require_contains(gltf_viewer, "render::pbr_debug_view_from_name(config_.debug_view)",
                     "glTF viewer should initialize PBR diagnostics from --debug-view");
    require_contains(gltf_viewer, "render::next_pbr_debug_view(debug_view_)",
                     "glTF viewer should let D cycle PBR diagnostics");
    require_contains(gltf_viewer, ".debug_view = debug_view_",
                     "glTF viewer should pass the current debug view to the renderer");

    require_contains(material_cubes, "render::pbr_debug_view_from_name(config_.debug_view)",
                     "material_cubes should initialize PBR diagnostics from --debug-view");
    require_contains(material_cubes, "render::next_pbr_debug_view(debug_view_)",
                     "material_cubes should let D cycle PBR diagnostics");
    require_contains(material_cubes, ".debug_view = debug_view_",
                     "material_cubes should pass the current debug view to the renderer");
}

void test_gltf_basisu_transcoder_policy_uses_bc7_and_rgba_fallback() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string basisu =
        read_source_file(source_root / "src/cubey/engine/gltf_basisu_texture.cpp");

    require_contains(basisu, "cTFBC7_RGBA",
                     "BasisU transcode policy should use BC7 for compressed desktop upload");
    require_contains(basisu, "cTFRGBA32",
                     "BasisU transcode policy should keep an RGBA8 fallback path");
    require_contains(basisu, "VK_FORMAT_BC7_SRGB_BLOCK",
                     "sRGB KTX2 textures should use BC7 sRGB when compressed");
    require_contains(basisu, "VK_FORMAT_R8G8B8A8_SRGB",
                     "sRGB KTX2 textures should use RGBA8 sRGB for fallback upload");
    require_contains(basisu, "bc7_available",
                     "BasisU transcode policy should make compressed upload conditional");
    require_contains(basisu, "basis_is_format_supported",
                     "BasisU transcode policy should fall back when a payload cannot use BC7");
}

void test_gltf_basisu_transcoder_uses_bundled_zstd() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string cmake = read_source_file(source_root / "CMakeLists.txt");

    require_contains(cmake, "zstd/zstd.c",
                     "BasisU transcoder should compile the pinned bundled Zstd source");
    require_not_contains(cmake, "pkg_check_modules(ZSTD",
                         "BasisU transcoder should not require a system Zstd package");
    require_not_contains(cmake, "PkgConfig::ZSTD",
                         "BasisU transcoder should not mix bundled headers with system Zstd");
}

void test_vulkan_and_gltf_sample_asset_cmake_paths_are_portable_and_pinned() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string root_cmake = read_source_file(source_root / "CMakeLists.txt");
    const std::string cubey_cmake = read_source_file(source_root / "src/cubey/CMakeLists.txt");

    require_contains(root_cmake, "find_package(Vulkan REQUIRED)",
                     "Vulkan discovery should use CMake's Vulkan package");
    require_not_contains(root_cmake, "pkg_check_modules(VULKAN_LOADER",
                         "Vulkan discovery should not require a vulkan.pc file");
    require_contains(cubey_cmake, "Vulkan::Vulkan",
                     "Vulkan library targets should link through the CMake Vulkan target");
    require_contains(root_cmake, "2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf",
                     "fetched glTF sample assets should be pinned to a known-good commit");
    require_not_contains(root_cmake, "GIT_TAG        main",
                         "glTF sample assets should not follow upstream main implicitly");
}
