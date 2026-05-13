#include <cubey/render/material.h>
#include <cubey/render/pbr.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_source_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open source file: " + path.string());
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

void require_contains(const std::string& text, const std::string& needle,
                      const char* message) {
    require(text.find(needle) != std::string::npos, message);
}

void require_not_contains(const std::string& text, const std::string& needle,
                          const char* message) {
    require(text.find(needle) == std::string::npos, message);
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
    require(pass.descriptor_sets[0].bindings.size() == 5,
            "PBR scene descriptors should include uniform, shadow map, and IBL textures");
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

void test_pbr_shaders_use_filament_style_material_remap() {
    const std::filesystem::path source_root{CUBEY_SOURCE_DIR};
    const std::string pbr = read_source_file(source_root / "shaders/cubey/pbr.glsl");
    const std::string furnace =
        read_source_file(source_root / "projects/pbr_furnace/shaders/pbr_furnace.frag");
    const std::string gltf =
        read_source_file(source_root / "projects/gltf_viewer/shaders/gltf_pbr.frag");

    require_contains(pbr, "CUBEY_PBR_DIELECTRIC_F0",
                     "PBR shader should define the dielectric F0 contract");
    require_contains(pbr, "cubey_pbr_diffuse_color",
                     "PBR shader should expose a baseColor-to-diffuse remap helper");
    require_contains(pbr, "cubey_pbr_f0",
                     "PBR shader should expose a baseColor-to-F0 remap helper");
    require_contains(pbr, "cubey_pbr_lambert_diffuse",
                     "PBR shader should expose a Lambert diffuse helper");

    for (const std::string* shader : {&furnace, &gltf}) {
        require_contains(*shader, "vec3 diffuse_color = cubey_pbr_diffuse_color(albedo, metallic);",
                         "PBR fragment shaders should compute diffuseColor explicitly");
        require_contains(*shader, "vec3 f0 = cubey_pbr_f0(albedo, metallic);",
                         "PBR fragment shaders should compute F0 through the shared helper");
        require_contains(*shader, "irradiance * diffuse_color",
                         "PBR indirect diffuse should use diffuseColor without Fresnel attenuation");
        require_not_contains(*shader, "(vec3(1.0) - ibl_f) * (1.0 - metallic)",
                             "PBR indirect diffuse should not double-attenuate metallic values");
        require_not_contains(*shader, "(vec3(1.0) - f) * (1.0 - metallic)",
                             "PBR direct diffuse should not double-attenuate metallic values");
    }

    require_contains(gltf, "cubey_pbr_lambert_diffuse(diffuse_color)",
                     "glTF direct diffuse should use the shared Lambert helper");
}
