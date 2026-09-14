#include "source_file_test_helpers.h"

#include <cubey/render/hdr_post_frame.h>
#include <cubey/render/material.h>
#include <cubey/render/pbr.h>
#include <cubey/render/pbr_material_resources.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

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
    require(pass.descriptor_sets[0].bindings.size() == 7,
            "PBR scene descriptors should include uniform, IBL, and refraction radiance textures");
    require(pass.descriptor_sets[0].bindings[5].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrSceneBinding::PreviousPrefilteredCube),
            "PBR scene descriptors should retain the previous environment cube");
    require(
        pass.descriptor_sets[0].bindings[6].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrSceneBinding::RefractionRadiance) &&
            pass.descriptor_sets[0].bindings[6].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        "PBR scene descriptors should reserve a sampled same-frame refraction radiance slot");
    require(pass.descriptor_sets[1].set == 1, "PBR material descriptors should use set 1");
    require(pass.descriptor_sets[1].bindings.size() == 18,
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
    require(pass.descriptor_sets[1].bindings[16].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Transmission),
            "PBR transmission texture should use binding 16");
    require(pass.descriptor_sets[1].bindings[17].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::VolumeThickness),
            "PBR volume thickness texture should use binding 17");
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

void test_pbr_material_schema_and_sampled_bindings_are_canonical() {
    const cubey::render::MaterialDescriptorSetLayout canonical =
        cubey::render::pbr_material_descriptor_set_layout();
    const cubey::render::MaterialPassInfo pass = cubey::render::pbr_forward_pass_info();
    const cubey::render::MaterialDescriptorSetLayout& pass_material_set = pass.descriptor_sets[1];
    const std::span<const cubey::render::PbrMaterialBinding> sampled_bindings =
        cubey::render::pbr_sampled_material_bindings();

    require(canonical.set == 1U, "canonical PBR material descriptors should use set 1");
    require(canonical.bindings.size() == sampled_bindings.size() + 1U,
            "canonical PBR material schema should include sampled bindings and uniforms");
    require(pass_material_set.set == canonical.set &&
                pass_material_set.bindings.size() == canonical.bindings.size(),
            "PBR forward pass should use the canonical material descriptor set");
    for (std::size_t index = 0; index < canonical.bindings.size(); ++index) {
        require(pass_material_set.bindings[index].binding == canonical.bindings[index].binding &&
                    pass_material_set.bindings[index].type == canonical.bindings[index].type &&
                    pass_material_set.bindings[index].stage_flags ==
                        canonical.bindings[index].stage_flags,
                "PBR forward pass material descriptors should match the canonical schema");
    }
    for (const cubey::render::PbrMaterialBinding binding : sampled_bindings) {
        const std::uint32_t binding_number = static_cast<std::uint32_t>(binding);
        require(canonical.bindings[binding_number].binding == binding_number &&
                    canonical.bindings[binding_number].type ==
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                "canonical PBR material schema should expose every sampled binding");
    }
    const std::uint32_t uniform_binding =
        static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Uniforms);
    require(canonical.bindings[uniform_binding].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "canonical PBR material schema should reserve the uniform binding");

    std::vector<cubey::render::SampledImageMaterialBinding> valid_bindings;
    valid_bindings.reserve(sampled_bindings.size());
    for (std::size_t index = 0; index < sampled_bindings.size(); ++index) {
        valid_bindings.push_back({
            .binding = static_cast<std::uint32_t>(sampled_bindings[index]),
            .sampler = reinterpret_cast<VkSampler>(0x100U + index),
            .image_view = reinterpret_cast<VkImageView>(0x200U + index),
        });
    }
    cubey::render::validate_pbr_sampled_image_bindings(valid_bindings);

    auto rejects = [&valid_bindings](const char* message, auto mutate) {
        std::vector<cubey::render::SampledImageMaterialBinding> invalid = valid_bindings;
        mutate(invalid);
        require_throws([&invalid] { cubey::render::validate_pbr_sampled_image_bindings(invalid); },
                       message);
    };
    rejects("PBR sampled binding validation should reject missing entries",
            [](auto& invalid) { invalid.pop_back(); });
    rejects("PBR sampled binding validation should reject duplicate entries",
            [](auto& invalid) { invalid.back().binding = invalid.front().binding; });
    rejects("PBR sampled binding validation should reject the uniform binding", [](auto& invalid) {
        invalid.back().binding =
            static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Uniforms);
    });
    rejects("PBR sampled binding validation should reject null samplers",
            [](auto& invalid) { invalid.front().sampler = VK_NULL_HANDLE; });
    rejects("PBR sampled binding validation should reject null image views",
            [](auto& invalid) { invalid.front().image_view = VK_NULL_HANDLE; });
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
    factors.dielectric_ior = 1.8F;
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
    factors.transmission_factor = 0.75F;
    factors.volume_thickness_factor = 0.35F;
    factors.volume_attenuation_color = {0.2F, 0.4F, 0.6F};
    factors.volume_attenuation_distance = 3.5F;
    factors.dispersion = 2.04F;
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
    factors.texture_flags |= cubey::render::pbr_material_texture_flag(
        cubey::render::PbrMaterialTextureFlag::Transmission);
    factors.texture_flags |= cubey::render::pbr_material_texture_flag(
        cubey::render::PbrMaterialTextureFlag::VolumeThickness);
    factors.texture_transforms.base_color.offset_scale = {0.25F, 0.5F, 2.0F, 3.0F};
    factors.texture_transforms.base_color.rotation_texcoord = {0.0F, 1.0F, 1.0F, 0.0F};
    factors.texture_transforms.normal.offset_scale = {0.1F, 0.2F, 0.5F, 0.75F};
    factors.texture_transforms.normal.rotation_texcoord = {1.0F, 0.0F, 0.0F, 0.0F};
    factors.texture_transforms.clearcoat.offset_scale = {0.3F, 0.4F, 0.5F, 0.6F};
    factors.texture_transforms.sheen_color.rotation_texcoord = {0.0F, 1.0F, 0.0F, 0.0F};
    factors.texture_transforms.iridescence_thickness.offset_scale = {0.7F, 0.8F, 0.9F, 1.0F};
    factors.texture_transforms.transmission.rotation_texcoord = {0.0F, 1.0F, 1.0F, 0.0F};
    factors.texture_transforms.volume_thickness.offset_scale = {0.8F, 0.7F, 0.6F, 0.5F};

    const cubey::render::PbrMaterialDefinition definition{
        .label = "uniform-test",
        .factors = factors,
        .alpha_mode = cubey::render::MaterialAlphaMode::Blend,
        .cull_mode = VK_CULL_MODE_NONE,
        .sort_key = 37,
    };
    const cubey::render::PbrMaterialUniforms uniforms =
        cubey::render::pbr_material_uniforms(definition);
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
    require(uniforms.material_model.x == factors.dielectric_ior,
            "PBR material uniforms should pack dielectric IOR");
    require(uniforms.material_model.y ==
                static_cast<float>(
                    static_cast<std::underlying_type_t<cubey::render::MaterialAlphaMode>>(
                        definition.alpha_mode)),
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
    require(uniforms.transmission_factor.x == factors.transmission_factor &&
                uniforms.transmission_factor.y == factors.dispersion,
            "PBR material uniforms should pack transmission and dispersion in the stable block");
    require(uniforms.volume_thickness_attenuation_distance.x == factors.volume_thickness_factor &&
                uniforms.volume_thickness_attenuation_distance.y ==
                    factors.volume_attenuation_distance,
            "PBR material uniforms should pack volume thickness and attenuation distance");
    require(uniforms.volume_attenuation_color.x == factors.volume_attenuation_color.x &&
                uniforms.volume_attenuation_color.y == factors.volume_attenuation_color.y &&
                uniforms.volume_attenuation_color.z == factors.volume_attenuation_color.z,
            "PBR material uniforms should pack RGB volume attenuation color");
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
    require(uniforms.texture_transforms.transmission.rotation_texcoord ==
                factors.texture_transforms.transmission.rotation_texcoord,
            "PBR material uniforms should pack transmission texture transform");
    require(uniforms.texture_transforms.volume_thickness.offset_scale ==
                factors.texture_transforms.volume_thickness.offset_scale,
            "PBR material uniforms should pack independent volume thickness texture transforms");
    const cubey::render::MaterialInfo info = cubey::render::pbr_material_info(definition);
    require(info.label == definition.label && info.alpha_mode == definition.alpha_mode &&
                info.cull_mode == definition.cull_mode && info.sort_key == definition.sort_key,
            "PBR material info should preserve the definition's explicit routing inputs");
    require(info.optical_mode == cubey::render::MaterialOpticalMode::Transmission &&
                info.blend == cubey::render::MaterialBlendMode::AlphaBlend &&
                !cubey::render::material_supports_pass(
                    info, cubey::render::MaterialPassKind::DepthOnly) &&
                cubey::render::material_supports_pass(
                    info, cubey::render::MaterialPassKind::ForwardColor),
            "positive transmission should derive optical routing independently from blend alpha");

    const cubey::render::PbrMaterialDefinition mask_definition{
        .label = "mask-test",
        .factors = {},
        .alpha_mode = cubey::render::MaterialAlphaMode::Mask,
    };
    const cubey::render::MaterialInfo mask_info = cubey::render::pbr_material_info(mask_definition);
    require(mask_info.blend == cubey::render::MaterialBlendMode::Opaque &&
                cubey::render::material_supports_pass(mask_info,
                                                      cubey::render::MaterialPassKind::DepthOnly),
            "masked materials should retain opaque blend and depth-only routing");

    const cubey::render::PbrMaterialDefinition opaque_definition{
        .label = "opaque-test",
        .factors = {},
    };
    const cubey::render::MaterialInfo opaque_info =
        cubey::render::pbr_material_info(opaque_definition);
    require(opaque_info.blend == cubey::render::MaterialBlendMode::Opaque &&
                opaque_info.optical_mode == cubey::render::MaterialOpticalMode::Opaque &&
                cubey::render::material_supports_pass(opaque_info,
                                                      cubey::render::MaterialPassKind::DepthOnly),
            "opaque definitions should derive the ordinary depth and forward routing");

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
    const std::span<const cubey::render::PbrDefaultTexturePhysicalSpec> physical_specs =
        cubey::render::pbr_default_texture_physical_specs();

    require(bindings.size() == cubey::render::kPbrDefaultTextureLogicalBindingCount,
            "PBR should expose every sampled material binding");
    require(specs.size() == bindings.size(), "PBR default specs should cover every texture slot");
    require(physical_specs.size() == cubey::render::kPbrDefaultTexturePhysicalCount,
            "PBR defaults should own exactly five physical fallback textures");
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        require(specs[i].binding == bindings[i],
                "PBR default texture specs should follow sampled binding order");
        require(specs[i].physical_id ==
                    cubey::render::pbr_default_texture_physical_id(bindings[i]),
                "each logical PBR binding should resolve through its declared physical fallback");
        const auto physical = std::find_if(
            physical_specs.begin(), physical_specs.end(), [&specs, i](const auto& candidate) {
                return candidate.id == specs[i].physical_id;
            });
        require(physical != physical_specs.end() && physical->rgba8 == specs[i].rgba8 &&
                    physical->format == specs[i].format,
                "logical PBR default diagnostics should derive their value from physical residency");
    }
    for (std::size_t i = 0; i < physical_specs.size(); ++i) {
        for (std::size_t j = i + 1U; j < physical_specs.size(); ++j) {
            require(physical_specs[i].id != physical_specs[j].id &&
                        (physical_specs[i].rgba8 != physical_specs[j].rgba8 ||
                         physical_specs[i].format != physical_specs[j].format),
                    "PBR physical fallback identities should have unique IDs and pixel-format pairs");
        }
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
    require(specs[15].binding == cubey::render::PbrMaterialBinding::Transmission &&
                specs[15].format == VK_FORMAT_R8G8B8A8_UNORM &&
                specs[15].rgba8 == std::array<std::uint8_t, 4>{255, 255, 255, 255},
            "transmission default should be a linear white scalar multiplier");
    require(specs[16].binding == cubey::render::PbrMaterialBinding::VolumeThickness &&
                specs[16].format == VK_FORMAT_R8G8B8A8_UNORM &&
                specs[16].rgba8 == std::array<std::uint8_t, 4>{255, 255, 255, 255},
            "volume thickness default should be a linear white green-channel multiplier");

    constexpr std::array<std::pair<cubey::render::PbrMaterialBinding,
                                   cubey::render::PbrDefaultTexturePhysicalId>,
                         cubey::render::kPbrDefaultTextureLogicalBindingCount>
        kExpectedPhysicalFallbacks{{
            {cubey::render::PbrMaterialBinding::BaseColor,
             cubey::render::PbrDefaultTexturePhysicalId::SrgbWhite},
            {cubey::render::PbrMaterialBinding::MetallicRoughness,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::Normal,
             cubey::render::PbrDefaultTexturePhysicalId::FlatTangentNormal},
            {cubey::render::PbrMaterialBinding::Occlusion,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::Emissive,
             cubey::render::PbrDefaultTexturePhysicalId::SrgbBlack},
            {cubey::render::PbrMaterialBinding::Specular,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::SpecularColor,
             cubey::render::PbrDefaultTexturePhysicalId::SrgbWhite},
            {cubey::render::PbrMaterialBinding::Clearcoat,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::ClearcoatRoughness,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::ClearcoatNormal,
             cubey::render::PbrDefaultTexturePhysicalId::FlatTangentNormal},
            {cubey::render::PbrMaterialBinding::SheenColor,
             cubey::render::PbrDefaultTexturePhysicalId::SrgbWhite},
            {cubey::render::PbrMaterialBinding::SheenRoughness,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::Anisotropy,
             cubey::render::PbrDefaultTexturePhysicalId::AnisotropyDefault},
            {cubey::render::PbrMaterialBinding::Iridescence,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::IridescenceThickness,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::Transmission,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
            {cubey::render::PbrMaterialBinding::VolumeThickness,
             cubey::render::PbrDefaultTexturePhysicalId::LinearWhite},
        }};
    for (const auto [binding, physical_id] : kExpectedPhysicalFallbacks) {
        require(cubey::render::pbr_default_texture_physical_id(binding) == physical_id,
                "each PBR sampled binding should use its intended physical fallback");
    }
    require_throws(
        [] { (void)cubey::render::pbr_default_texture_physical_id(
              cubey::render::PbrMaterialBinding::Uniforms); },
        "PBR uniforms should remain outside the sampled default texture contract");
}

void test_pbr_material_table_requires_complete_immutable_records() {
    cubey::render::PbrMaterialTable table;
    const cubey::render::MaterialHandle material{.index = 4, .generation = 2};

    require(!table.contains(material), "empty PBR material table should contain no records");
    require(!table.initialized(), "empty PBR material table should start uninitialized");
    require(!table.metrics().initialized,
            "uninitialized PBR material table metrics should report no residency");
    require_throws([&table, material] { (void)table.definition(material); },
                   "PBR material table should not expose a definition without its record");
    require_throws([&table] { (void)table.descriptor_set_layout(); },
                   "PBR material table should require initialization for its canonical layout");
    require_throws(
        [&table, material] {
            (void)table.emplace(material, cubey::render::PbrMaterialDefinition{}, {});
        },
        "PBR material table should not publish a partial record before initialization");
    require_throws([&table, material] { table.erase(material); },
                   "PBR material table should reject erasing a partial or absent record");
    require_throws([&table, material] { table.rebind(material, {.index = 7, .generation = 1}); },
                   "PBR material table should reject rebinding a partial or absent record");
    table.clear();
    require(!table.contains(material), "PBR material clear should preserve an empty table");
    require(!table.initialized(),
            "PBR material clear should retire its canonical descriptor-layout residency");
    const cubey::render::PbrMaterialTableMetrics cleared_metrics = table.metrics();
    require(!cleared_metrics.initialized && cleared_metrics.material_count == 0U &&
                cleared_metrics.allocated_descriptor_set_count == 0U &&
                cleared_metrics.block_count == 0U && cleared_metrics.descriptor_pool_count == 0U &&
                cleared_metrics.uniform_buffer_count == 0U &&
                cleared_metrics.uniform_stride == 0U &&
                cleared_metrics.uniform_block_byte_size == 0U &&
                cleared_metrics.allocated_uniform_byte_size == 0U,
            "PBR material clear should report no remaining pooled residency");
    require_throws([&table] { (void)table.descriptor_set_layout(); },
                   "PBR material clear should require reinitialization before layout access");
    require_throws(
        [&table, material] {
            (void)table.emplace(material, cubey::render::PbrMaterialDefinition{}, {});
        },
        "PBR material clear should require reinitialization before publication");
}

void test_pbr_material_table_public_api_is_immutable() {
    const std::string header = read_source_file(
        std::filesystem::path{CUBEY_SOURCE_DIR} / "include/cubey/render/pbr_material_resources.h");

    require_not_contains(header, "set_factors",
                         "PBR material table should not permit factor mutation after publication");
    require_not_contains(header, "void upload(MaterialHandle",
                         "PBR material table should not expose per-draw uniform uploads");
    require_not_contains(header, "emplace_instance",
                         "PBR material table should not permit separately publishing an instance");
}

void test_pbr_material_uniform_block_layout_is_checked() {
    const cubey::render::PbrMaterialTableConfig default_config{};
    require(default_config.block_capacity == cubey::render::kDefaultPbrMaterialBlockCapacity,
            "PBR material table defaults should retain bounded block capacity");

    const cubey::render::PbrMaterialUniformBlockLayout aligned =
        cubey::render::pbr_material_uniform_block_layout(sizeof(cubey::render::PbrMaterialUniforms),
                                                         256U, 64U);
    require(aligned.uniform_stride == 768U,
            "PBR material uniform stride should satisfy uniform-buffer alignment");
    require(aligned.uniform_block_byte_size == 49'152U,
            "PBR material uniform block should reserve one aligned slot per material");

    const cubey::render::PbrMaterialUniformBlockLayout zero_alignment =
        cubey::render::pbr_material_uniform_block_layout(sizeof(cubey::render::PbrMaterialUniforms),
                                                         0U, 2U);
    require(zero_alignment.uniform_stride == sizeof(cubey::render::PbrMaterialUniforms) &&
                zero_alignment.uniform_block_byte_size ==
                    2U * sizeof(cubey::render::PbrMaterialUniforms),
            "zero uniform-buffer alignment should safely behave as alignment one");
    require_throws([] { (void)cubey::render::pbr_material_uniform_block_layout(0U, 1U, 1U); },
                   "PBR material pooled uniforms should reject zero-size records");
    require_throws([] { (void)cubey::render::pbr_material_uniform_block_layout(1U, 1U, 0U); },
                   "PBR material pooled uniforms should reject zero-capacity blocks");
    require_throws(
        [] {
            (void)cubey::render::pbr_material_uniform_block_layout(
                std::numeric_limits<VkDeviceSize>::max(), 1U, 2U);
        },
        "PBR material pooled uniforms should reject overflowing block byte sizes");

    static_assert(std::is_move_constructible_v<cubey::render::PbrMaterialTable>);
    static_assert(!std::is_copy_constructible_v<cubey::render::PbrMaterialTable>);
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
    require(pass.descriptor_sets[0].bindings.size() == 3,
            "PBR skybox pass should declare uniforms and blended environment cubes");
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
    require(pass.descriptor_sets[0].bindings[2].binding == 2,
            "PBR skybox previous environment should use binding 2");
    require(pass.descriptor_sets[0].bindings[2].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            "PBR skybox previous environment should be a sampled image");
    require(pass.push_constants.empty(), "PBR skybox pass should not use push constants");
}

void test_pbr_ior_helpers_preserve_glTF_dialect() {
    require(cubey::render::pbr_f0_from_ior(0.0F) == 1.0F,
            "IOR zero should use glTF's infinite-IOR compatibility mode");
    require(cubey::render::pbr_f0_from_ior(1.0F) == 0.0F,
            "IOR one should have zero normal-incidence reflectance");
    const float default_f0 = cubey::render::pbr_f0_from_ior(1.5F);
    require(default_f0 > 0.0399F && default_f0 < 0.0401F,
            "IOR 1.5 should map to dielectric F0 0.04");
    require(cubey::render::pbr_f0_from_ior(2.42F) > 0.16F,
            "high dielectric IOR should not be clipped through a reflectance control");
}

void test_pbr_shaders_use_gltf_material_remap() {
    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string pbr = read_source_file(source_root / "shaders/cubey/pbr.glsl");
    const std::string post =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr_post.frag");
    const std::string furnace =
        read_source_file(source_root / "projects/pbr_furnace/shaders/pbr_furnace.frag");
    const std::string furnace_header =
        read_source_file(source_root / "projects/pbr_furnace/pbr_furnace_app_internal.h");
    const std::string furnace_render =
        read_source_file(source_root / "projects/pbr_furnace/pbr_furnace_render.cpp");
    const std::string furnace_resources =
        read_source_file(source_root / "projects/pbr_furnace/pbr_furnace_resources.cpp");
    const std::string gltf =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr.frag");
    const std::string gltf_vertex =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr.vert");
    const std::string forward_pbr_skybox =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr_skybox.frag");
    const std::string gltf_shadow =
        read_source_file(source_root / "shaders/cubey/forward_pbr/forward_pbr_shadow_depth.frag");
    const std::string gltf_materials =
        read_source_file(source_root / "src/cubey/engine/gltf_scene_importer_materials.cpp");

    for (const std::string* shader : {&gltf, &gltf_shadow, &furnace}) {
        require_contains(
            *shader, "vec4 transmission_factor;",
            "all PBR shader variants should preserve the shared transmission ABI block");
    }

    require_contains(pbr, "cubey_pbr_diffuse_color",
                     "PBR shader should expose a baseColor-to-diffuse remap helper");
    require_contains(pbr, "cubey_pbr_f0",
                     "PBR shader should expose a baseColor-to-F0 remap helper");
    require_contains(pbr, "cubey_pbr_f0_from_ior",
                     "PBR shader should expose the glTF IOR-to-F0 helper");
    require_contains(pbr, "cubey_pbr_dielectric_f0",
                     "PBR shader should expose dielectric F0 material extension helper");
    require_contains(pbr, "if (ior == 0.0)",
                     "PBR shader should preserve glTF IOR zero compatibility mode");
    require_contains(pbr, "f90 * dfg.g",
                     "PBR shader should use the material F90 endpoint for indirect specular");
    require_contains(pbr, "cubey_pbr_lambert_diffuse",
                     "PBR shader should expose a Lambert diffuse helper");
    require_contains(pbr, "cubey_pbr_direct_transmission_btdf",
                     "PBR shader should expose the reusable direct transmission BTDF helper");
    require_contains(pbr, "vec3 mirrored_light = l - (2.0 * n * dot(l, n));",
                     "direct transmission should use Khronos mirrored-light construction");
    require_contains(pbr, "cubey_pbr_transmission_screen_perceptual_roughness",
                     "PBR shader should retain its explicit screen-pyramid perceptual mapping");
    require_contains(pbr, "cubey_pbr_direct_transmission_perceptual_roughness",
                     "direct transmission should expose an alpha-domain IOR conversion");
    require_contains(pbr,
                     "float scaled_alpha = clamped_perceptual_roughness * "
                     "clamped_perceptual_roughness * ior_scale;",
                     "direct transmission should apply IOR scale once to Khronos alpha roughness");
    require_contains(pbr, "return sqrt(scaled_alpha);",
                     "direct transmission should convert scaled alpha back for Cubey GGX helpers");
    require_contains(pbr, "cubey_pbr_distribution_ggx(ndoth, direct_perceptual_roughness)",
                     "direct transmission should use isotropic GGX distribution");
    require_contains(pbr,
                     "cubey_pbr_visibility_smith_ggx_correlated(\n        ndotv, ndotl, "
                     "direct_perceptual_roughness)",
                     "direct transmission should use correlated GGX visibility");
    require_contains(pbr, "cubey_pbr_apply_volume_attenuation",
                     "PBR shader should share the Beer-Lambert attenuation helper");
    require_contains(pbr, "cubey_pbr_clearcoat_layer_weight",
                     "PBR shader should expose the fixed-IOR clearcoat layering helper");
    require_contains(pbr, "return vec3(dfg.b);",
                     "clearcoat IBL should use the Fresnel-free DFG single-scatter channel");
    require_contains(pbr, "cubey_pbr_visibility_smith_ggx_correlated_anisotropic",
                     "PBR shader should expose anisotropic GGX visibility");
    require_contains(pbr, "return clamp(visibility, 0.0, 1.0);",
                     "anisotropic GGX visibility should remain bounded at grazing angles");
    require_contains(pbr, "cubey_pbr_anisotropic_bent_normal",
                     "PBR shader should expose the anisotropic IBL bent-normal heuristic");
    require_contains(pbr, "Preserve its raw intermediate\n    // cross-product scale",
                     "anisotropic IBL should retain the Khronos bent-normal cross-product scale");
    require_contains(pbr, "cubey_pbr_apply_display_transform",
                     "PBR shader should expose a final display transform helper");
    require_contains(pbr, "cubey_pbr_iridescence_sensitivity",
                     "PBR shader should expose the Khronos thin-film sensitivity fit");
    require_contains(
        pbr, "cubey_pbr_iridescence_fresnel(float outside_ior, float film_ior, float cos_theta1",
        "PBR shader should expose angle-dependent thin-film Fresnel");
    require_contains(pbr, "for (int order = 1; order <= 2; ++order)",
                     "thin-film Fresnel should retain the two-order Khronos interference series");
    require_contains(pbr, "if (thickness_nm <= 0.0)",
                     "thin-film Fresnel should preserve a zero-thickness identity guard");
    require_contains(pbr, "cubey_pbr_lambda_sheen_numeric_helper",
                     "PBR shader should expose the Estevez-Kulla sheen visibility fit");
    require_contains(pbr, "21.5473",
                     "PBR sheen visibility should retain the ratified Khronos fit coefficients");
    require_contains(pbr, "cubey_pbr_visibility_sheen",
                     "PBR shader should use full Charlie sheen visibility");
    require_contains(post, "cubey_pbr_apply_display_transform(color, post.display_transform)",
                     "PBR post shader should apply display transform to the HDR scene color");
    require_contains(post, "uniform sampler2D scene_color",
                     "PBR post shader should sample the scene color texture");

    require_contains(furnace, "vec4 display_transform",
                     "PBR furnace should keep direct display transform controls");
    require_contains(furnace, "cubey_pbr_apply_display_transform(color, scene.display_transform)",
                     "PBR furnace should keep direct display transform output");
    require_contains(furnace, "cubey_pbr_visibility_smith_ggx_correlated_anisotropic",
                     "PBR furnace should exercise anisotropic GGX visibility");
    require_contains(furnace, "cubey_pbr_anisotropic_bent_normal",
                     "PBR furnace should exercise anisotropic IBL bent-normal lookup");
    require_contains(furnace, "scene.light_color_intensity.a > 0.0",
                     "PBR furnace anisotropy witness should add fixed directional lighting");
    require_contains(furnace, "base_direct *= clearcoat_attenuation",
                     "PBR furnace clearcoat should attenuate its layered direct base response");
    require_contains(furnace, "sheen_direct + (base_direct * sheen_direct_attenuation)",
                     "PBR furnace should mirror direct sheen energy layering");
    require_contains(furnace, "sheen_prefiltered * sheen_color * sheen_view_energy",
                     "PBR furnace should mirror the DFG-backed sheen IBL response");
    require_contains(furnace, "cubey_pbr_clearcoat_direct(",
                     "PBR furnace clearcoat should evaluate its outer direct lobe");
    require_contains(furnace, "(base_direct * ndotl) + (clearcoat_direct * clearcoat_ndotl)",
                     "PBR furnace clearcoat should compose base and coat direct lobes separately");
    require_contains(furnace, "cubey_pbr_direct_transmission_btdf",
                     "PBR furnace should exercise the shared direct transmission BTDF");
    require_contains(furnace, "direct += direct_transmission_layer - (transmission * diffuse_direct_contribution)",
                     "PBR furnace should replace rather than add transmission-weighted diffuse light");
    require_not_contains(furnace, "dot(-normal, view_direction)",
                         "PBR furnace should not invert normals for thin-film evaluation");
    require_contains(furnace_header, "pbr_furnace_forward_pass_info",
                     "PBR furnace should centralize its double-sided material pass");
    require_contains(furnace_header, "pass.cull_mode = VK_CULL_MODE_NONE",
                     "PBR furnace should retain both primitive windings for depth selection");
    require_contains(furnace_render, ".material_pass = pbr_furnace_forward_pass_info()",
                     "PBR furnace pipeline should use its double-sided material pass");
    require_contains(furnace_resources, ".material_pass = pbr_furnace_forward_pass_info()",
                     "PBR furnace descriptors should use the same double-sided material pass");

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
                         "PBR fragment shaders should read material dielectric IOR");
        require_contains(*shader, "cubey_pbr_has_material_texture",
                         "PBR fragment shaders should branch optional texture reads by flag");
        require_contains(*shader, "vec3 f0 = cubey_pbr_f0(albedo, metallic, dielectric_f0);",
                         "PBR fragment shaders should compute F0 through the shared helper");
        require_contains(
            *shader,
            "vec3 f90 = mix(vec3(cubey_pbr_saturate(specular_strength)), vec3(1.0), metallic);",
            "PBR fragment shaders should preserve glTF specular F90 behavior");
        require_contains(*shader, "cubey_pbr_fresnel_schlick(ndotv, f0, f90)",
                         "PBR indirect diffuse should use the material Fresnel endpoints");
        require_contains(*shader, "diffuse_ibl_attenuation",
                         "PBR indirect diffuse should be attenuated by dielectric Fresnel");
        require_contains(*shader, "cubey_pbr_indirect_specular(f0, f90, dfg)",
                         "PBR indirect specular should use the material Fresnel endpoints");
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
    require_contains(gltf, "material.clearcoat_transform)).r",
                     "glTF clearcoat factor texture should use its linear red channel");
    require_contains(gltf, "material.clearcoat_roughness_transform)).g",
                     "glTF clearcoat roughness texture should use its linear green channel");
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
    require_contains(gltf, "uniform sampler2D refraction_radiance",
                     "glTF PBR shader should bind same-frame HDR refraction radiance");
    require_contains(gltf, "uniform sampler2D transmission_texture",
                     "glTF PBR shader should bind the KHR_materials_transmission texture");
    require_contains(gltf, "transmission *= texture(transmission_texture",
                     "glTF transmission should multiply factor by texture red");
    require_contains(gltf, "transmission *= 1.0 - metallic",
                     "glTF transmission should suppress metallic base interfaces");
    require_contains(gltf, "cubey_pbr_transmission_screen_perceptual_roughness",
                     "glTF screen transmission should use its dedicated perceptual IOR mapping");
    require_contains(pbr,
                     "return perceptual_roughness * clamp((dielectric_ior * 2.0) - 2.0, 0.0, "
                     "1.0);",
                     "glTF screen transmission should preserve its established perceptual IOR mapping");
    require_contains(gltf, "float perceptual_roughness = clamp(",
                     "glTF transmission should retain authored perceptual roughness before BRDF "
                     "flooring");
    require_contains(gltf, "float roughness = max(perceptual_roughness, 0.04);",
                     "the BRDF roughness floor should not erase smooth transmission mip zero");
    require_contains(gltf, "cubey_pbr_transmission_pyramid_lod(perceptual_roughness",
                     "glTF transmission should map raw perceptual roughness into the HDR pyramid");
    require_contains(gltf, "float q = clamp(screen_perceptual_roughness, 0.0, 1.0);",
                     "transmission pyramid LOD should clamp its remapped roughness input");
    require_contains(gltf,
                     "float max_lod = float(max(textureQueryLevels(refraction_radiance) - 1, 0));",
                     "transmission pyramid LOD should use the screen pyramid mip domain");
    require_contains(gltf, "if (q <= 0.0 || max_lod <= 0.0)",
                     "smooth transmission and a one-level pyramid should remain at exact mip zero");
    require_contains(
        gltf, "float chain_variance_span = max(exp2(2.0 * max_lod) - 1.0, 0.0);",
        "transmission pyramid LOD should derive its variance span from the 2x mip chain");
    require_contains(
        gltf, "float variance_ratio = max(1.0 + (q * q * q * q) * chain_variance_span, 1.0);",
        "transmission pyramid LOD should map microfacet alpha variance into the bounded chain");
    require_contains(
        gltf, "return clamp(0.5 * log2(variance_ratio), 0.0, max_lod);",
        "transmission pyramid LOD should remain finite and bounded by the screen chain");
    require_not_contains(gltf, "roughness * roughness * ior_roughness_scale",
                         "transmission must not retain the pre-Khronos squared roughness mapping");
    require_contains(gltf, "textureLod(refraction_radiance",
                     "glTF transmission should sample the same-frame HDR pyramid directly");
    require_contains(
        gltf, "cubey_pbr_transmission_environment_lod(perceptual_roughness",
        "glTF transmission should map fallback roughness in the environment LOD domain");
    require_contains(gltf, "cubey_pbr_prefiltered_environment(fallback_direction, environment_lod)",
                     "glTF transmission should not reuse a screen-pyramid LOD for the environment");
    require_not_contains(
        gltf, "cubey_pbr_prefiltered_environment(fallback_direction, pyramid_lod)",
        "glTF transmission fallback should keep screen and cube mip domains separate");
    require_contains(gltf, "return mix(fallback_radiance, screen_radiance, screen_weight)",
                     "glTF transmission should smoothly fade toward the edge fallback");
    require_contains(gltf, "transmitted_radiance * base_color.rgb * transmission",
                     "glTF transmission should tint transmitted body radiance by base color");
    require_contains(gltf, "vec3 direct_transmitted_radiance = cubey_pbr_direct_transmission_btdf(",
                     "glTF transmission should evaluate direct BTDF through the shared helper");
    require_contains(gltf, "direct_transmitted_radiance *= radiance * visibility;",
                     "glTF direct BTDF should use the selected light and opaque-shadow visibility");
    require_contains(
        gltf,
        "vec3 direct_transmission_layer = direct_transmitted_radiance * transmission *\n"
        "                                         interface_transmittance * sheen_view_attenuation *\n"
        "                                         clearcoat_attenuation;",
        "glTF direct BTDF should apply transmission, interface, and layered attenuation once");
    require_contains(gltf, "color += transmission_layer + direct_transmission_layer - (transmission * replaced_diffuse);",
                     "glTF transmission should replace the diffuse budget instead of adding direct BTDF energy");
    require_contains(gltf, "uniform sampler2D volume_thickness_texture",
                     "glTF PBR shader should bind the KHR_materials_volume thickness texture");
    require_contains(
        gltf, "material.volume_thickness_transform)).g",
        "glTF volume thickness should read its linear green channel through its own transform");
    require_contains(gltf, "struct CubeyPbrVolumeTransmissionRay",
                     "glTF volume should return an explicit transmission-ray result");
    require_contains(gltf, "vec3 world_offset;",
                     "glTF volume ray results should carry the refracted world offset");
    require_contains(gltf, "vec3 fallback_direction;",
                     "glTF volume ray results should carry the fallback direction");
    require_contains(gltf, "float world_path_distance;",
                     "glTF volume ray results should carry the world optical distance");
    require_contains(gltf, "cubey_pbr_volume_transmission_ray",
                     "glTF volume should construct a Filament-inspired solid-volume refraction-ray "
                     "representation");
    require_contains(gltf, "float eta_ir = 1.0 / interface_ior",
                     "glTF volume should derive the entry refraction ratio from the material IOR");
    require_contains(gltf, "float entry_discriminant = 1.0 - (eta_ir * eta_ir * sin2_theta)",
                     "glTF volume should guard degenerate entry refraction and TIR");
    require_contains(gltf, "vec3 internal_direction =",
                     "glTF volume should retain the refracted interior direction");
    require_contains(gltf, "float mesh_path_distance = max(thickness, 0.0) *",
                     "glTF volume should derive an interior path from the authored thickness");
    require_contains(gltf, "vec3 mesh_exit_offset = internal_direction * mesh_path_distance",
                     "glTF volume should advance to the approximate second interface");
    require_contains(
        gltf, "vec3 exit_direction = refract(internal_direction, exit_normal, interface_ior)",
        "glTF volume should refract through the approximate second interface");
    require_contains(gltf, "exit_direction = reflect(internal_direction, exit_normal)",
                     "glTF volume should keep a finite fallback when the exit interface TIRs");
    require_contains(gltf, "mesh_exit_offset * max(model_scale, vec3(0.0))",
                     "glTF volume thickness should be scaled from mesh to world space");
    require_contains(gltf, "cubey_pbr_project_refraction_exit",
                     "glTF volume should project its refracted exit into same-frame screen space");
    require_contains(gltf, "frag_world_position + volume_transmission_ray.world_offset",
                     "glTF volume should project the refracted world-space exit point");
    require_contains(gltf,
                     "refraction_fallback_direction = volume_transmission_ray.fallback_direction",
                     "glTF volume should use the refracted ray for environment fallback");
    require_contains(gltf, "result.world_path_distance = length(result.world_offset)",
                     "glTF volume should measure attenuation in world-space path distance");
    require_contains(
        gltf, "result.fallback_direction = exit_direction * inversesqrt(exit_length_squared)",
        "glTF volume should use the second-interface direction for environment fallback");
    require_not_contains(
        gltf, "result.fallback_direction = result.world_offset / result.world_path_distance",
        "glTF volume fallback should not reuse the screen-projection offset direction");
    require_contains(gltf, "cubey_pbr_apply_volume_attenuation",
                     "glTF volume should apply Beer-Lambert attenuation after radiance lookup");
    require_contains(pbr, "pow(clamp(attenuation_color, 0.0, 1.0)",
                     "glTF volume attenuation should preserve per-channel attenuation color");
    require_contains(gltf, "if (volume_thickness > 0.0)",
                     "glTF thickness zero should preserve the existing thin transmission path");
    require_contains(gltf, "out float representative_world_ray_length",
                     "dispersion should expose its representative volume path to direct transmission");
    require_contains(gltf, "representative_world_ray_length = r1.world_path_distance;",
                     "direct transmission should reuse the 546.1nm representative volume path");
    require_contains(gltf, "direct_transmitted_radiance, direct_volume_ray_length,",
                     "direct transmission should attenuate through the shared representative path");
    require_contains(gltf, "const mat3 K0 = mat3(",
                     "glTF dispersion should use Filament's first spectral integration matrix");
    require_contains(gltf, "-0.45422013, 0.04493517, 0.98249798",
                     "glTF dispersion K0 should preserve Filament's exact column-major literals");
    require_contains(gltf, "const mat3 K1 = mat3(",
                     "glTF dispersion should use Filament's second spectral integration matrix");
    require_contains(gltf, "0.06839811, 0.02732891,  0.01602064",
                     "glTF dispersion K1 should preserve Filament's exact column-major literals");
    require_contains(gltf, "const mat3 K2 = mat3(",
                     "glTF dispersion should use Filament's third spectral integration matrix");
    require_contains(gltf, "0.31884400, -0.05627069, 0.00083808",
                     "glTF dispersion K2 should preserve Filament's exact column-major literals");
    require_contains(gltf, "const mat3 K3 = mat3(",
                     "glTF dispersion should use Filament's fourth spectral integration matrix");
    require_contains(gltf, "0.06697807, -0.01599341, 0.00064333",
                     "glTF dispersion K3 should preserve Filament's exact column-major literals");
    require_contains(
        gltf, "const float offsets[4] = float[](0.70795215, 0.24790980, 0.00000000, -0.29204785);",
        "glTF dispersion should preserve Filament's exact optimized wavelength offsets");
    require_contains(gltf, "float dispersion_factor = (dispersion / 20.0) * (base_ior - 1.0);",
                     "glTF dispersion should derive spectral IOR offsets from the base IOR");
    require_contains(gltf, "float ior0 = max(1.0, base_ior + dispersion_factor * offsets[0]);",
                     "glTF dispersion should clamp each spectral IOR to the air interface");
    for (const char* ray_name : {"r0", "r1", "r2", "r3"}) {
        require_contains(gltf,
                         std::string("CubeyPbrVolumeTransmissionRay ") + ray_name +
                             " = cubey_pbr_volume_transmission_ray(",
                         "glTF dispersion should use four explicit Cubey volume rays");
    }
    require_contains(gltf, "cubey_pbr_transmission_radiance_at_lod",
                     "glTF dispersion should share explicit screen and environment LODs");
    require_contains(
        gltf,
        "float pyramid_lod = cubey_pbr_transmission_pyramid_lod(perceptual_roughness, base_ior);",
        "glTF dispersion screen LOD should be derived from the base material IOR");
    require_contains(
        gltf,
        "float environment_lod = cubey_pbr_transmission_environment_lod(perceptual_roughness, "
        "base_ior);",
        "glTF dispersion environment LOD should be derived from the base material IOR");
    require_contains(gltf, "r1.world_path_distance / attenuation_distance",
                     "glTF dispersion should use the 546.1nm ray for representative attenuation");
    require_contains(
        gltf, "s0 *= transmittance;",
        "glTF dispersion should attenuate the first spectral RGB sample before integration");
    require_contains(gltf, "s1 *= transmittance;",
                     "glTF dispersion should attenuate the representative spectral RGB sample "
                     "before integration");
    require_contains(
        gltf, "s2 *= transmittance;",
        "glTF dispersion should attenuate the third spectral RGB sample before integration");
    require_contains(
        gltf, "s3 *= transmittance;",
        "glTF dispersion should attenuate the fourth spectral RGB sample before integration");
    require_contains(
        gltf, "return max(K0 * s0 + K1 * s1 + K2 * s2 + K3 * s3, vec3(0.0));",
        "glTF dispersion should integrate complete RGB samples with Filament's matrices");
    require_not_contains(gltf, "channel_radiance[channel]",
                         "glTF dispersion should not collapse spectral samples into one channel");
    require_not_contains(gltf, "interleavedGradientNoise",
                         "glTF dispersion should not introduce temporal or spatial jitter");
    require_contains(gltf, "bool volume_attenuation_pending = true;",
                     "glTF dispersion should track whether the caller still owes Beer attenuation");
    require_contains(gltf, "volume_attenuation_pending = false;",
                     "glTF dispersion should mark its in-branch Beer attenuation as complete");
    require_contains(gltf, "if (volume_attenuation_pending)",
                     "glTF dispersion should prevent caller-side double attenuation");
    require_contains(gltf, "if (material.transmission_factor.y > 0.0)",
                     "glTF dispersion should be opt-in within the thick-volume branch");
    require_contains(gltf, "if (volume_thickness <= 0.0 || material.transmission_factor.y <= 0.0)",
                     "zero dispersion should preserve the one-sample volume path exactly");
    require_contains(gltf, "volume_ray_length = volume_transmission_ray.world_path_distance",
                     "glTF dispersion should retain the green/base ray length for attenuation");

    // The four GLSL matrices are column-major. Transpose the constructor literals into row
    // storage here and verify both their identity sum and white preservation. This catches a
    // transposed/corrupted literal without asserting any raster output or fragile sample order.
    using Matrix3 = std::array<std::array<double, 3>, 3>;
    const Matrix3 k0{{{{0.00581637, -0.11782236, -0.45422013}},
                      {{0.02312851, 0.11316202, 0.04493517}},
                      {{0.01689631, 0.11098148, 0.98249798}}}};
    const Matrix3 k1{{{{0.14291703, -0.27560148, 0.06839811}},
                      {{0.10429778, 0.57678541, 0.02732891}},
                      {{-0.01556522, -0.06412244, 0.01602064}}}};
    const Matrix3 k2{{{{0.70106120, 0.29545674, 0.31884400}},
                      {{-0.09440402, 0.29931852, -0.05627069}},
                      {{-0.00241699, -0.04351961, 0.00083808}}}};
    const Matrix3 k3{{{{0.15020522, 0.09796715, 0.06697807}},
                      {{-0.03302213, 0.01073410, -0.01599341}},
                      {{0.00108589, -0.00333946, 0.00064333}}}};
    const std::array<Matrix3, 4> matrices{k0, k1, k2, k3};
    Matrix3 matrix_sum{};
    for (const Matrix3& matrix : matrices) {
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                matrix_sum[row][column] += matrix[row][column];
            }
        }
    }
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            const double expected = row == column ? 1.0 : 0.0;
            require(std::fabs(matrix_sum[row][column] - expected) < 2.0e-6,
                    "glTF dispersion matrix sum should preserve the RGB identity transform");
        }
        const double white = matrix_sum[row][0] + matrix_sum[row][1] + matrix_sum[row][2];
        require(std::fabs(white - 1.0) < 2.0e-6,
                "glTF dispersion matrix sum should preserve white radiance");
    }
    require_contains(gltf, "view_facing_fresnel = cubey_pbr_fresnel_schlick(ndotv, f0, f90)",
                     "glTF transmission should derive its interface Fresnel from the view angle");
    require_contains(gltf,
                     "view_interface_fresnel = mix(view_facing_fresnel, thin_film_fresnel, "
                     "iridescence_factor)",
                     "glTF transmission should retain the iridescent view-facing interface");
    require_contains(gltf, "vec3 interface_transmittance = vec3(1.0) - view_interface_fresnel",
                     "glTF transmission should preserve per-channel RGB interface transmittance");
    require_contains(
        gltf, "diffuse_direct_contribution",
        "glTF transmission should identify direct diffuse independently from specular");
    require_contains(gltf, "base_diffuse_ibl",
                     "glTF transmission should identify diffuse IBL independently from specular");
    require_contains(gltf, "legacy_ambient_diffuse",
                     "glTF transmission should identify legacy ambient diffuse independently");
    require_contains(
        gltf,
        "color += transmission_layer + direct_transmission_layer - (transmission * "
        "replaced_diffuse);",
        "glTF transmission should replace only diffuse sources while preserving layers");
    require_contains(gltf, "cubey_pbr_transformed_uv(material.specular_transform)",
                     "glTF PBR shader should sample specular strength through transformed UVs");
    require_contains(gltf, "cubey_pbr_transformed_uv(material.specular_color_transform)",
                     "glTF PBR shader should sample specular color through transformed UVs");
    require_contains(gltf, "cubey_pbr_clearcoat_direct",
                     "glTF PBR shader should evaluate clearcoat direct lighting");
    require_contains(gltf, "float clearcoat_layer_weight =",
                     "glTF PBR shader should evaluate one clearcoat Fresnel layer weight");
    require_contains(gltf, "cubey_pbr_clearcoat_layer_weight(clearcoat_factor, clearcoat_ndotv)",
                     "glTF PBR shader should share its clearcoat Fresnel between base and layer");
    require_contains(gltf, "cubey_pbr_clearcoat_indirect(clearcoat_dfg)",
                     "glTF PBR shader should use the Fresnel-free clearcoat IBL lobe");
    require_not_contains(gltf, "cubey_pbr_indirect_specular(vec3(0.04)",
                         "glTF clearcoat IBL should not apply a second integrated Fresnel term");
    require_contains(gltf, "(emissive * clearcoat_attenuation)",
                     "glTF clearcoat should attenuate emission below the coat layer");
    require_contains(gltf,
                     "diffuse_ibl_attenuation * sheen_view_attenuation *\n"
                     "                                  clearcoat_attenuation * occlusion",
                     "glTF sheen and clearcoat should attenuate ambient diffuse below both layers");
    require_contains(gltf, "if (clearcoat_factor > 0.0)",
                     "glTF clearcoat factor zero should skip its normal and roughness path");
    require_contains(gltf_materials,
                     "source.clearcoat_texture, asset::GltfTextureColorSpace::Linear",
                     "glTF clearcoat factor texture should preserve linear color space");
    require_contains(gltf_materials,
                     "source.clearcoat_roughness_texture,\n"
                     "                                 asset::GltfTextureColorSpace::Linear",
                     "glTF clearcoat roughness texture should preserve linear color space");
    require_contains(gltf_materials,
                     "source.clearcoat_normal_texture,\n"
                     "                                 asset::GltfTextureColorSpace::Linear",
                     "glTF clearcoat normal texture should preserve linear color space");
    require_contains(gltf_materials,
                     "source.anisotropy_texture, asset::GltfTextureColorSpace::Linear",
                     "glTF anisotropy texture should preserve linear color space");
    require_contains(gltf_materials,
                     "source.iridescence_texture, asset::GltfTextureColorSpace::Linear",
                     "glTF iridescence factor texture should preserve linear color space");
    require_contains(gltf_materials,
                     "source.iridescence_thickness_texture,\n"
                     "                                 asset::GltfTextureColorSpace::Linear",
                     "glTF iridescence thickness texture should preserve linear color space");
    require_contains(gltf_materials,
                     "source.transmission_texture, asset::GltfTextureColorSpace::Linear",
                     "glTF transmission texture should preserve linear red-channel semantics");
    require_contains(
        gltf_materials,
        "source.volume_thickness_texture,\n"
        "                                 asset::GltfTextureColorSpace::Linear",
        "glTF volume thickness texture should preserve linear green-channel semantics");
    require_contains(gltf_materials,
                     "source.sheen_color_texture, asset::GltfTextureColorSpace::Srgb",
                     "glTF sheen color texture should preserve sRGB transfer semantics");
    require_contains(gltf_materials,
                     "source.sheen_roughness_texture,\n"
                     "                                 asset::GltfTextureColorSpace::Linear",
                     "glTF sheen roughness texture should preserve linear alpha semantics");
    require_contains(gltf, "cubey_pbr_sheen_direct",
                     "glTF PBR shader should evaluate sheen direct lighting");
    require_contains(gltf, "if (sheen_color_max > 0.0)",
                     "glTF PBR shader should preserve an exact zero-color base path");
    require_contains(gltf, "texture(brdf_lut, vec2(ndotv, sheen_roughness)).a",
                     "glTF PBR shader should source sheen directional albedo from DFG alpha");
    require_contains(gltf, "sheen_direct + (base_direct * sheen_direct_attenuation)",
                     "glTF PBR shader should energy-layer direct sheen over its full base");
    require_contains(
        gltf, "sheen_prefiltered * sheen_color * sheen_view_energy",
        "glTF PBR shader should use roughness-filtered environment radiance for sheen");
    require_contains(gltf, "vec3 sheen_reflection = reflect(-view_direction, normal)",
                     "glTF sheen IBL should use the isotropic material-normal reflection");
    require_contains(gltf, "base_ibl *= sheen_view_attenuation",
                     "glTF sheen IBL should energy-scale the complete iridescent base response");
    require_contains(gltf, "cubey_pbr_distribution_ggx_anisotropic",
                     "glTF PBR shader should evaluate anisotropic specular");
    require_contains(gltf, "cubey_pbr_visibility_smith_ggx_correlated_anisotropic",
                     "glTF PBR shader should evaluate anisotropic GGX visibility");
    require_contains(gltf, "mix(alpha_roughness, 1.0, anisotropy_strength * anisotropy_strength)",
                     "glTF PBR shader should derive tangent roughness from anisotropy strength");
    require_contains(
        gltf, "texture_direction = (anisotropy_sample.rg * 2.0) - 1.0",
        "glTF anisotropy texture should map red-green direction from [0, 1] to [-1, 1]");
    require_contains(gltf, "anisotropy_strength *= anisotropy_sample.b",
                     "glTF anisotropy texture blue channel should modulate strength");
    require_contains(gltf, "vec3 ibl_normal = anisotropy_strength > 0.0",
                     "glTF anisotropy should preserve the isotropic IBL normal at zero strength");
    require_contains(gltf, "cubey_pbr_anisotropic_bent_normal",
                     "glTF anisotropy should use the shared bent-normal IBL heuristic");
    require_contains(gltf, "material.iridescence_transform)).r",
                     "glTF iridescence factor texture should use its linear red channel");
    require_contains(gltf, "material.iridescence_thickness_transform)).g",
                     "glTF iridescence thickness texture should use its linear green channel");
    require_contains(gltf, "if (iridescence_thickness <= 0.0)",
                     "glTF iridescence should make zero thickness neutral");
    require_contains(gltf, "iridescence_fresnel_dielectric = cubey_pbr_iridescence_fresnel",
                     "glTF iridescence should evaluate the specular-adjusted dielectric interface");
    require_contains(gltf, "iridescence_thickness, dielectric_f0)",
                     "glTF dielectric thin-film Fresnel should use the adjusted dielectric F0");
    require_contains(gltf, "iridescence_thickness, albedo)",
                     "glTF metallic thin-film Fresnel should use base color");
    require_contains(gltf, "mix(ordinary_fresnel, thin_film_fresnel, iridescence_factor)",
                     "glTF direct iridescence should mix ordinary and thin-film Fresnel by factor");
    require_contains(gltf,
                     "diffuse_ibl_base, prefiltered * specular_occlusion,\n"
                     "            iridescence_fresnel_dielectric",
                     "glTF iridescence IBL should apply dielectric thin-film Fresnel once");
    require_contains(gltf, "base_ibl = mix(base_ibl, thin_film_base_ibl, iridescence_factor)",
                     "glTF iridescence IBL should blend the existing ordinary response by factor");
    require_contains(gltf, "base_ibl + sheen_ibl) * clearcoat_attenuation",
                     "glTF clearcoat should remain outside the iridescence base response");
    require_contains(gltf_vertex, "orthogonalizeTangent",
                     "glTF PBR vertex shader should re-orthogonalize normal-map tangent frames");
    require_contains(gltf_vertex, "mat3(model) * in_tangent.xyz",
                     "glTF PBR vertex shader should transform tangents with the model linear part");
    require_contains(gltf_vertex, "frag_model_scale = vec3(length(push_constants.model[0].xyz)",
                     "glTF PBR vertex shader should provide model scale for volume thickness");
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
    require(shader_count == 9, "shared forward PBR package should contain nine shader files");
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
    require_contains(cmake, "UnlitTest/glTF/UnlitTest.gltf",
                     "glTF viewer sample smoke tests should cover unlit materials");
    require_contains(cmake, "EmissiveStrengthTest/glTF/EmissiveStrengthTest.gltf",
                     "glTF viewer sample smoke tests should cover emissive strength");
    require_contains(cmake, "ClearCoatTest/glTF/ClearCoatTest.gltf",
                     "glTF viewer sample smoke tests should cover clearcoat factors and textures");
    require_contains(cmake, "TransmissionTest/glTF/TransmissionTest.gltf",
                     "glTF viewer sample smoke tests should cover required transmission materials");
    require_contains(
        cmake, "TransmissionRoughnessTest/glTF/TransmissionRoughnessTest.gltf",
        "glTF viewer sample smoke tests should cover transmission roughness mip selection");
    require_contains(cmake, "TransmissionOrderTest/glTF/TransmissionOrderTest.gltf",
                     "glTF viewer sample smoke tests should cover transmission ordering");
    require_contains(cmake, "TransmissionOrderTest/glTF-Binary/TransmissionOrderTest.glb",
                     "glTF viewer conformance fixtures should pin transmission ordering");
    require_contains(
        cmake, "gltf-transmission-order-test",
        "glTF viewer conformance fixtures should semantically analyze transmission ordering");
    require_contains(
        cmake, "d904b6cd6c83792fd4a4d9ad4f0366bde76a63e347541c465f2ad4c5baf22a21",
        "TransmissionOrderTest conformance should pin the exact Khronos asset payload");
    require_contains(cmake,
                     "--pbr-environment-source static\n"
                     "            --no-clouds\n"
                     "            ${ARGN}",
                     "glTF sample smoke helper should forward opt-in per-sample capture controls");
    require_contains(cmake,
                     "\"ClearCoatTest/glTF/ClearCoatTest.gltf\"\n"
                     "        \"gltf-viewer-clearcoat-test-smoke.png\"\n"
                     "        --ibl-intensity\n"
                     "        0.5\n"
                     "        --exposure\n"
                     "        0\n"
                     "        --capture-camera-distance-scale\n"
                     "        0.55\n"
                     "        --capture-camera-yaw\n"
                     "        0\n"
                     "        --capture-camera-pitch\n"
                     "        0\n"
                     "        --time-of-day-mode\n"
                     "        manual\n"
                     "        --sun-elevation\n"
                     "        14\n"
                     "        --sun-azimuth\n"
                     "        180\n"
                     "        --pause-time",
                     "ClearCoatTest smoke should keep its reviewable fixed capture framing");
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
    require_contains(cmake, "CompareIridescence/glTF/CompareIridescence.gltf",
                     "glTF viewer sample smoke tests should cover required iridescence materials");
    require_contains(cmake, "SheenTestGrid/glTF/SheenTestGrid.gltf",
                     "glTF viewer sample smoke tests should cover required sheen materials");
    require_contains(cmake,
                     "\"SheenTestGrid/glTF/SheenTestGrid.gltf\"\n"
                     "        \"gltf-viewer-sheen-test-grid-smoke.png\"\n"
                     "        --capture-camera-distance-scale\n"
                     "        0.2\n"
                     "        --capture-camera-yaw\n"
                     "        0\n"
                     "        --capture-camera-pitch\n"
                     "        0",
                     "SheenTestGrid smoke should retain useful front-on 64-pixel framing");
    require_contains(cmake, "StainedGlassLamp/glTF-KTX-BasisU/StainedGlassLamp.gltf",
                     "glTF viewer sample smoke tests should cover KTX2 alpha/emissive textures");
    require_contains(cmake, "--no-clouds",
                     "glTF sample smoke tests should avoid cloud-dependent captures");

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
    const std::span<const cubey::render::PbrDefaultTextureSpec> logical_specs =
        cubey::render::pbr_default_texture_specs();
    const auto metallic_roughness = std::find_if(
        logical_specs.begin(), logical_specs.end(), [](const auto& spec) {
            return spec.binding == cubey::render::PbrMaterialBinding::MetallicRoughness;
        });
    require(metallic_roughness != logical_specs.end(),
            "shared PBR defaults should expose a metallic-roughness fallback");
    require(metallic_roughness->physical_id ==
                cubey::render::PbrDefaultTexturePhysicalId::LinearWhite &&
                metallic_roughness->rgba8 == std::array<std::uint8_t, 4>{255, 255, 255, 255} &&
                metallic_roughness->format == VK_FORMAT_R8G8B8A8_UNORM,
            "metallic-roughness fallback should leave roughness and metallic channels at one");
}

void test_pbr_examples_and_gltf_importer_share_material_resources() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string importer_header =
        read_source_file(source_root / "include/cubey/engine/gltf_scene_importer.h");
    const std::string furnace_header =
        read_source_file(source_root / "projects/pbr_furnace/pbr_furnace_app_internal.h");
    const std::string material_cubes =
        read_source_file(source_root / "examples/material_cubes/material_cubes_app_internal.h");

    require_contains(importer_header, "render::PbrMaterialTable materials",
                     "glTF import resources should expose a shared PBR material table");
    require_contains(importer_header, "std::optional<render::PbrDefaultTextureSet>",
                     "glTF import resources should own the shared PBR default texture set");

    require_contains(furnace_header, "render::PbrMaterialTable materials_",
                     "PBR furnace should retain the canonical material residency table");
    require_contains(furnace_header, "render::PbrDefaultTextureSet",
                     "PBR furnace should own one shared default texture set");

    require_contains(material_cubes, "cubey::render::PbrMaterialTable materials_",
                     "material cubes should retain the canonical material residency table");
    require_contains(material_cubes, "cubey::render::PbrDefaultTextureSet",
                     "material cubes should own one shared default texture set");
}

void test_pbr_consumers_use_atmosphere_lighting_foundation() {
    const std::filesystem::path source_root = std::filesystem::path{CUBEY_SOURCE_DIR};
    const std::string gltf_header =
        read_source_file(source_root / "projects/gltf_viewer/gltf_viewer_app_internal.h");
    const std::string ocean_ui = read_source_file(source_root / "projects/ocean/ocean_ui.cpp");
    const std::string atmosphere_ui =
        read_source_file(source_root / "include/cubey/host/atmosphere_environment_ui.h");
    const std::string pbr_docs = read_source_file(source_root / "docs/architecture/pbr-ibl.md");

    require_contains(atmosphere_ui, "draw_atmosphere_environment_controls",
                     "shared atmosphere UI should expose reusable environment controls");
    require_contains(gltf_header, "AtmosphereEnvironmentRuntime atmosphere_runtime_",
                     "glTF viewer should own a shared atmosphere environment runtime");
    require_contains(gltf_header, "GltfViewerEnvironmentPolicy environment_policy_",
                     "glTF viewer should resolve environment behavior once per run");
    require_not_contains(gltf_header, "AtmosphereDiffuseSource",
                         "glTF viewer should not expose multiple atmosphere diffuse paths");
    require_contains(ocean_ui, "draw_atmosphere_environment_controls",
                     "ocean should consume the shared atmosphere UI controls");
    require_contains(pbr_docs, "runtime atmosphere reflection probe",
                     "PBR docs should capture the current atmosphere lighting boundary");
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
