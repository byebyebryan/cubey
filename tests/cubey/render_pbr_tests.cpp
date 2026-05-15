#include "source_file_test_helpers.h"

#include <cubey/render/material.h>
#include <cubey/render/pbr.h>

#include <vulkan/vulkan.h>

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

void test_pbr_forward_pass_declares_scene_and_material_sets() {
    const cubey::render::MaterialPassInfo pass = cubey::render::pbr_forward_pass_info();
    require(pass.label == "pbr.forward", "PBR pass should use stable label");
    require(pass.kind == cubey::render::MaterialPassKind::ForwardColor,
            "PBR pass should be forward color");
    require(pass.depth_test && pass.depth_write, "PBR pass should enable depth");
    require(pass.descriptor_sets.size() == 2, "PBR pass should declare scene and material sets");
    require(pass.descriptor_sets[0].set == 0, "PBR scene descriptors should use set 0");
    require(pass.descriptor_sets[0].bindings.size() == 5,
            "PBR scene descriptors should include uniform, shadow map, and IBL textures");
    require(pass.descriptor_sets[1].set == 1, "PBR material descriptors should use set 1");
    require(pass.descriptor_sets[1].bindings.size() == 6,
            "PBR material descriptors should include textures plus material uniforms");
    require(pass.descriptor_sets[1].bindings[5].binding ==
                static_cast<std::uint32_t>(cubey::render::PbrMaterialBinding::Uniforms),
            "PBR material uniforms should use the final material descriptor binding");
    require(pass.descriptor_sets[1].bindings[5].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            "PBR material uniforms should be a uniform buffer");
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
    require(alpha_pass.blend_enable, "PBR alpha pass should enable color blending");
    require(alpha_pass.src_color_blend_factor == VK_BLEND_FACTOR_ONE,
            "PBR alpha pass should use premultiplied source color");
    require(alpha_pass.dst_color_blend_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "PBR alpha pass should destination blend from inverse alpha");
    require(alpha_pass.src_alpha_blend_factor == VK_BLEND_FACTOR_ONE,
            "PBR alpha pass should preserve source alpha");
    require(alpha_pass.dst_alpha_blend_factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            "PBR alpha pass should composite alpha with inverse source alpha");
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
    factors.alpha_mode = cubey::render::MaterialAlphaMode::Blend;
    factors.unlit = true;
    factors.texture_transforms.base_color.offset_scale = {0.25F, 0.5F, 2.0F, 3.0F};
    factors.texture_transforms.base_color.rotation_texcoord = {0.0F, 1.0F, 1.0F, 0.0F};
    factors.texture_transforms.normal.offset_scale = {0.1F, 0.2F, 0.5F, 0.75F};
    factors.texture_transforms.normal.rotation_texcoord = {1.0F, 0.0F, 0.0F, 0.0F};

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
                static_cast<float>(static_cast<std::underlying_type_t<
                                   cubey::render::MaterialAlphaMode>>(factors.alpha_mode)),
            "PBR material uniforms should pack alpha mode");
    require(uniforms.material_model.z == 1.0F, "PBR material uniforms should pack unlit flag");
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
    const cubey::render::PbrMaterialUniforms opaque_uniforms =
        cubey::render::pbr_material_uniforms(factors, cubey::render::MaterialAlphaMode::Opaque);
    require(opaque_uniforms.material_model.y == 0.0F,
            "PBR material uniforms should allow render policy to override factor alpha mode");

    const cubey::render::PbrPushConstants constants =
        cubey::render::pbr_push_constants(cubey::math::Mat4{1.0F});
    require(constants.model == cubey::math::Mat4{1.0F},
            "PBR push constants should carry only the model matrix");
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
    const std::string post = read_source_file(source_root / "shaders/cubey/pbr_post.frag");
    const std::string furnace =
        read_source_file(source_root / "projects/pbr_furnace/shaders/pbr_furnace.frag");
    const std::string gltf =
        read_source_file(source_root / "projects/gltf_viewer/shaders/gltf_pbr.frag");
    const std::string gltf_skybox =
        read_source_file(source_root / "projects/gltf_viewer/shaders/gltf_skybox.frag");
    const std::string gltf_shadow =
        read_source_file(source_root / "projects/gltf_viewer/shaders/gltf_shadow_depth.frag");

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
    require_not_contains(gltf_skybox, "cubey_pbr_apply_display_transform",
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
    require_contains(gltf, "float output_alpha = material.material_model.y > 1.5 ? "
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
    require_contains(gltf_shadow, "uniform sampler2D base_color_texture",
                     "glTF shadow mask shader should sample base color alpha");
    require_contains(gltf_shadow, "uniform PbrMaterialUniforms",
                     "glTF shadow mask shader should read per-material alpha cutoff");
    require_contains(gltf_shadow, "cubey_pbr_transformed_uv",
                     "glTF shadow mask shader should apply base-color texture transform");
    require_contains(gltf_shadow, "discard",
                     "glTF shadow mask shader should discard fragments below alpha cutoff");
}
