#include "gltf_asset_test_support.h"

using namespace cubey::test_support;

void test_gltf_asset_loads_static_pbr_triangle() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_triangle");
    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(write_triangle_gltf(dir));

    require(asset.scenes.size() == 1, "asset should load one scene");
    require(asset.default_scene == 0, "asset should preserve default scene");
    require(asset.nodes.size() == 1, "asset should load one node");
    require(asset.meshes.size() == 1, "asset should load one mesh");
    require(asset.materials.size() == 2, "asset should include default plus glTF material");
    require(asset.images.size() == 1, "asset should decode embedded PNG image");
    require(asset.images[0].width == 1 && asset.images[0].height == 1,
            "embedded image should decode to 1x1");

    const cubey::asset::GltfMaterial& material = asset.materials[1];
    require(material.alpha_mode == cubey::asset::GltfAlphaMode::Mask,
            "material should preserve alpha mode");
    require(material.double_sided, "material should preserve double-sided flag");
    require(material.base_color_texture.texture_index == 0,
            "material should map base color texture");
    require_close(material.base_color_factor.r, 0.8F, "base color factor should load");
    require_close(material.metallic_factor, 0.2F, "metallic factor should load");
    require_close(material.roughness_factor, 0.4F, "roughness factor should load");
    require_close(material.ior, 1.8F, "IOR extension should preserve the authored value");
    require_close(material.specular_factor, 0.7F, "specular factor should load");
    require_close(material.specular_color_factor.r, 0.9F, "specular color factor red should load");
    require_close(material.specular_color_factor.g, 0.8F,
                  "specular color factor green should load");
    require_close(material.specular_color_factor.b, 0.7F, "specular color factor blue should load");
    require(material.specular_texture.texture_index == 1,
            "specular texture should load from KHR_materials_specular");
    require(material.specular_color_texture.texture_index == 2,
            "specular color texture should load from KHR_materials_specular");
    require_close(material.emissive_factor.r, 0.2F, "emissive strength should scale red");
    require_close(material.emissive_factor.g, 0.4F, "emissive strength should scale green");
    require_close(material.emissive_factor.b, 0.6F, "emissive strength should scale blue");
    require_close(material.clearcoat_factor, 0.6F, "clearcoat factor should load");
    require_close(material.clearcoat_roughness_factor, 0.25F,
                  "clearcoat roughness factor should load");
    require(material.clearcoat_texture.texture_index == 3,
            "clearcoat texture should load from KHR_materials_clearcoat");
    require(material.clearcoat_roughness_texture.texture_index == 4,
            "clearcoat roughness texture should load from KHR_materials_clearcoat");
    require(material.clearcoat_normal_texture.texture_index == 5,
            "clearcoat normal texture should load from KHR_materials_clearcoat");
    require_close(material.clearcoat_normal_scale, 0.8F,
                  "clearcoat normal texture scale should load");
    require_close(material.sheen_color_factor.r, 0.2F, "sheen color factor red should load");
    require_close(material.sheen_color_factor.g, 0.3F, "sheen color factor green should load");
    require_close(material.sheen_color_factor.b, 0.4F, "sheen color factor blue should load");
    require_close(material.sheen_roughness_factor, 0.45F, "sheen roughness factor should load");
    require(material.sheen_color_texture.texture_index == 6,
            "sheen color texture should load from KHR_materials_sheen");
    require(material.sheen_roughness_texture.texture_index == 7,
            "sheen roughness texture should load from KHR_materials_sheen");
    require_close(material.anisotropy_strength, 0.55F, "anisotropy strength should load");
    require_close(material.anisotropy_rotation, 1.25F, "anisotropy rotation should load");
    require(material.anisotropy_texture.texture_index == 8,
            "anisotropy texture should load from KHR_materials_anisotropy");
    require_close(material.iridescence_factor, 0.65F, "iridescence factor should load");
    require_close(material.iridescence_ior, 1.4F, "iridescence IOR should load");
    require_close(material.iridescence_thickness_minimum, 120.0F,
                  "iridescence thickness minimum should load");
    require_close(material.iridescence_thickness_maximum, 520.0F,
                  "iridescence thickness maximum should load");
    require(material.iridescence_texture.texture_index == 9,
            "iridescence texture should load from KHR_materials_iridescence");
    require(material.iridescence_thickness_texture.texture_index == 10,
            "iridescence thickness texture should load from KHR_materials_iridescence");

    const cubey::asset::GltfMeshPrimitive& primitive = asset.meshes[0].primitives[0];
    require(primitive.vertices.size() == 3, "primitive should load vertices");
    require(primitive.indices == std::vector<std::uint32_t>({0, 1, 2}),
            "primitive should load uint16 indices as uint32");
    require(primitive.material_index == 1, "primitive material should account for default slot");
    require_close(primitive.vertices[1].position.x, 1.0F, "position accessor should load");
    require_close(primitive.vertices[2].texcoord0.y, 1.0F, "uv accessor should load");
    require_close(primitive.vertices[0].tangent.x, 1.0F, "loader should generate missing tangents");
    require_close(primitive.local_bounds.center.x, 0.5F, "bounds center should be computed");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_collects_exclusive_load_phase_timings() {
    using Clock = std::chrono::steady_clock;
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_load_profile");
    const std::filesystem::path path = write_triangle_gltf(dir);
    cubey::asset::GltfAssetLoadProfile profile{
        .document_parse_milliseconds = -1.0,
        .buffer_load_milliseconds = -1.0,
        .asset_validate_milliseconds = -1.0,
        .image_payload_milliseconds = -1.0,
        .image_decode_milliseconds = -1.0,
        .asset_assembly_milliseconds = -1.0,
    };
    const Clock::time_point started = Clock::now();
    const cubey::asset::GltfAsset profiled_asset =
        cubey::asset::load_gltf_asset(path, {}, &profile);
    const double inclusive_milliseconds =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    const cubey::asset::GltfAsset unprofiled_asset = cubey::asset::load_gltf_asset(path);

    const std::array<double, 6> phases{
        profile.document_parse_milliseconds, profile.buffer_load_milliseconds,
        profile.asset_validate_milliseconds, profile.image_payload_milliseconds,
        profile.image_decode_milliseconds,   profile.asset_assembly_milliseconds,
    };
    double phase_total = 0.0;
    for (const double phase : phases) {
        require(phase >= 0.0, "every glTF load profile phase should be nonnegative");
        phase_total += phase;
    }
    require(phase_total <= inclusive_milliseconds + 1.0,
            "exclusive glTF load phases should not materially exceed inclusive wall time");
    require(profiled_asset.meshes.size() == unprofiled_asset.meshes.size() &&
                profiled_asset.images.size() == unprofiled_asset.images.size() &&
                profiled_asset.materials.size() == unprofiled_asset.materials.size(),
            "optional profiling output should not change glTF asset construction");

    const std::filesystem::path malformed_path = dir / "malformed.gltf";
    write_text_file(malformed_path, "not JSON");
    require_throws(
        [&] { static_cast<void>(cubey::asset::load_gltf_asset(malformed_path, {}, &profile)); },
        "profiled parser failure should retain existing loader failure behavior");
    for (const double phase : std::array<double, 6>{
             profile.document_parse_milliseconds,
             profile.buffer_load_milliseconds,
             profile.asset_validate_milliseconds,
             profile.image_payload_milliseconds,
             profile.image_decode_milliseconds,
             profile.asset_assembly_milliseconds,
         }) {
        require(phase >= 0.0,
                "failed profiled loads should reset output to nonnegative partial timings");
    }

    const std::filesystem::path ktx_path = dir / "profile-ktx2.gltf";
    cubey::write_binary_file(dir / "profile-ktx2.ktx2", minimal_ktx2_header(4, 4, 1));
    write_text_file(ktx_path, R"JSON({
  "asset": {"version": "2.0"},
  "images": [{"uri": "profile-ktx2.ktx2", "mimeType": "image/ktx2"}]
})JSON");
    const cubey::asset::GltfAsset ktx_asset = cubey::asset::load_gltf_asset(ktx_path, {}, &profile);
    require(ktx_asset.images.size() == 1 &&
                ktx_asset.images[0].encoding == cubey::asset::GltfImageEncoding::Ktx2Basisu,
            "KTX2 profiling fixture should remain encoded");
    require(profile.image_decode_milliseconds == 0.0,
            "KTX2 header validation must not be reported as image decode time");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_ignores_unknown_optional_extensions() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_optional_extension");
    const std::filesystem::path path = dir / "optional_extension.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["VENDOR_optional_debug_data"]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.materials.size() == 1, "loader should still create the default material");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_rejects_unknown_required_extensions() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_required_extension");
    const std::filesystem::path path = dir / "required_extension.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["VENDOR_required_geometry"],
  "extensionsRequired": ["VENDOR_required_geometry"]
})JSON");

    require_throws([&path] { (void)cubey::asset::load_gltf_asset(path); },
                   "loader should reject unknown required extensions");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_accepts_closed_required_extensions() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_supported_required_extension");
    const std::filesystem::path path = dir / "supported_required_extension.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_emissive_strength",
    "KHR_materials_unlit",
    "KHR_materials_clearcoat",
    "KHR_materials_anisotropy",
    "KHR_materials_iridescence",
    "KHR_materials_sheen",
    "KHR_texture_transform",
    "KHR_texture_basisu"
  ],
  "extensionsRequired": [
    "KHR_materials_emissive_strength",
    "KHR_materials_unlit",
    "KHR_materials_clearcoat",
    "KHR_materials_anisotropy",
    "KHR_materials_iridescence",
    "KHR_materials_sheen",
    "KHR_texture_transform",
    "KHR_texture_basisu"
  ],
  "materials": [
  {
    "emissiveFactor": [0.2, 0.3, 0.4],
    "extensions": {
      "KHR_materials_emissive_strength": {"emissiveStrength": 3.0}
    }
  },
  {
    "extensions": {"KHR_materials_unlit": {}}
  },
  {
    "extensions": {"KHR_materials_clearcoat": {
      "clearcoatFactor": 0.5,
      "clearcoatRoughnessFactor": 0.2
    }}
  },
  {
    "extensions": {"KHR_materials_anisotropy": {
      "anisotropyStrength": 0.5,
      "anisotropyRotation": 0.25
    }}
  },
  {
    "extensions": {"KHR_materials_iridescence": {
      "iridescenceFactor": 0.5,
      "iridescenceIor": 1.4,
      "iridescenceThicknessMinimum": 520.0,
      "iridescenceThicknessMaximum": 120.0
    }}
  },
  {
    "extensions": {"KHR_materials_sheen": {
      "sheenColorFactor": [0.2, 0.3, 0.4],
      "sheenRoughnessFactor": 0.6
    }}
  }]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.materials.size() == 7, "loader should preserve required-extension materials");
    require_close(asset.materials[1].emissive_factor.g, 0.9F,
                  "closed required emissive-strength extension should load");
    require(asset.materials[2].unlit, "closed required unlit extension should load");
    require_close(asset.materials[3].clearcoat_factor, 0.5F,
                  "closed required clearcoat extension should load its factor");
    require_close(asset.materials[4].anisotropy_strength, 0.5F,
                  "closed required anisotropy extension should load its factor");
    require_close(asset.materials[5].iridescence_factor, 0.5F,
                  "closed required iridescence extension should load its factor");
    require_close(asset.materials[5].iridescence_thickness_minimum, 520.0F,
                  "closed required iridescence should permit minimum above maximum");
    require_close(asset.materials[6].sheen_roughness_factor, 0.6F,
                  "closed required sheen extension should load its roughness factor");
    std::filesystem::remove_all(dir);
}
