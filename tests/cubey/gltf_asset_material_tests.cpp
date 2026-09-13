#include "gltf_asset_test_support.h"

using namespace cubey::test_support;

void test_gltf_asset_preserves_ior_special_and_high_values() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_ior_values");
    const std::filesystem::path path = dir / "ior_values.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_ior"],
  "materials": [
    {"extensions": {"KHR_materials_ior": {"ior": 0.0}}},
    {"extensions": {"KHR_materials_ior": {"ior": 2.42}}}
  ]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.materials.size() == 3, "loader should preserve IOR test materials");
    require_close(asset.materials[1].ior, 0.0F,
                  "loader should preserve the IOR zero compatibility sentinel");
    require_close(asset.materials[2].ior, 2.42F,
                  "loader should not clip high dielectric IOR values");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_validates_ior_and_specular_material_values() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_material_extension_values");
    const std::filesystem::path path = dir / "material_extension_values.gltf";

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_ior", "KHR_materials_specular"],
  "extensionsRequired": ["KHR_materials_ior", "KHR_materials_specular"],
  "materials": [
    {"extensions": {"KHR_materials_ior": {"ior": 0.0}}},
    {"extensions": {"KHR_materials_ior": {"ior": 2.42}}},
    {"extensions": {"KHR_materials_specular": {
      "specularFactor": 0.35,
      "specularColorFactor": [1.25, 0.5, 0.0]
    }}}
  ]
})JSON");
    const cubey::asset::GltfAsset valid = cubey::asset::load_gltf_asset(path);
    require_close(valid.materials[1].ior, 0.0F, "valid IOR zero compatibility value should load");
    require_close(valid.materials[2].ior, 2.42F, "valid high IOR should load");
    require_close(valid.materials[3].specular_color_factor.r, 1.25F,
                  "finite nonnegative specular color should not be silently clamped");

    const auto require_invalid = [&path](std::string_view extension) {
        write_text_file(path, std::string{"{\n  \"asset\": {\"version\": \"2.0\"},\n"} +
                                  "  \"extensionsUsed\": [\"" + std::string{extension} +
                                  "\"],\n  \"materials\": [{\"extensions\": {\"" +
                                  std::string{extension} + "\": " +
                                  (extension == "KHR_materials_ior" ? "{\"ior\": 0.5}"
                                                                    : "{\"specularFactor\": 1.1}") +
                                  "}}]\n}\n");
        require_throws_with_message(
            [&path] { (void)cubey::asset::load_gltf_asset(path); }, extension,
            "invalid material extension value should identify its rejected extension");
    };
    require_invalid("KHR_materials_ior");
    require_invalid("KHR_materials_specular");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_specular"],
  "materials": [{"extensions": {"KHR_materials_specular": {
    "specularFactor": 0.5,
    "specularColorFactor": [-0.01, 0.5, 0.5]
  }}}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "specularColorFactor",
        "negative specular color should be rejected with a field-specific error");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_closes_clearcoat_material_contract() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_clearcoat_contract");
    const std::filesystem::path path = dir / "clearcoat_contract.gltf";
    constexpr std::string_view kTinyPng =
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
        "x8AAwMCAO+/p9sAAAAASUVORK5CYII=";

    write_text_file(path, std::string(R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_clearcoat", "KHR_texture_transform"],
  "extensionsRequired": ["KHR_materials_clearcoat", "KHR_texture_transform"],
  "materials": [
    {"extensions": {"KHR_materials_clearcoat": {}}},
    {"extensions": {"KHR_materials_clearcoat": {
      "clearcoatFactor": 0.6,
      "clearcoatTexture": {
        "index": 0,
        "texCoord": 1,
        "extensions": {"KHR_texture_transform": {
          "offset": [0.2, 0.3], "rotation": 0.5, "scale": [0.4, 0.5], "texCoord": 0
        }}
      },
      "clearcoatRoughnessFactor": 0.25,
      "clearcoatRoughnessTexture": {"index": 1, "extensions": {
        "KHR_texture_transform": {"offset": [0.6, 0.7], "scale": [0.8, 0.9]}
      }},
      "clearcoatNormalTexture": {"index": 2, "scale": 0.8, "extensions": {
        "KHR_texture_transform": {"offset": [0.1, 0.9], "rotation": 0.25}
      }}
    }}}
  ],
  "textures": [{"source": 0}, {"source": 0}, {"source": 0}],
  "images": [{"uri": ")JSON") +
                              std::string{kTinyPng} + R"JSON("}]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);
    require(asset.materials.size() == 3,
            "clearcoat fixture should retain its implicit and authored materials");
    const cubey::asset::GltfMaterial& defaults = asset.materials[1];
    require_close(defaults.clearcoat_factor, 0.0F,
                  "clearcoatFactor should default to a disabled layer");
    require_close(defaults.clearcoat_roughness_factor, 0.0F,
                  "clearcoatRoughnessFactor should default to zero");
    require_close(defaults.clearcoat_normal_scale, 1.0F,
                  "clearcoat normal texture scale should default to one");
    require(!defaults.clearcoat_texture.has_value() &&
                !defaults.clearcoat_roughness_texture.has_value() &&
                !defaults.clearcoat_normal_texture.has_value(),
            "default clearcoat material should not invent texture references");

    const cubey::asset::GltfMaterial& material = asset.materials[2];
    require_close(material.clearcoat_factor, 0.6F,
                  "clearcoatFactor should preserve the authored linear multiplier");
    require_close(material.clearcoat_roughness_factor, 0.25F,
                  "clearcoatRoughnessFactor should preserve the authored linear multiplier");
    require(material.clearcoat_texture.texture_index == 0 &&
                material.clearcoat_roughness_texture.texture_index == 1 &&
                material.clearcoat_normal_texture.texture_index == 2,
            "clearcoat channels should preserve their independent source textures");
    require(material.clearcoat_texture.texcoord == 0,
            "clearcoat texture transform should override the source texture coordinate set");
    require_close(material.clearcoat_texture.offset.x, 0.2F,
                  "clearcoat texture transform should preserve offset x");
    require_close(material.clearcoat_texture.offset.y, 0.3F,
                  "clearcoat texture transform should preserve offset y");
    require_close(material.clearcoat_texture.rotation, 0.5F,
                  "clearcoat texture transform should preserve rotation");
    require_close(material.clearcoat_texture.scale.x, 0.4F,
                  "clearcoat texture transform should preserve scale x");
    require_close(material.clearcoat_texture.scale.y, 0.5F,
                  "clearcoat texture transform should preserve scale y");
    require_close(material.clearcoat_roughness_texture.offset.x, 0.6F,
                  "clearcoat roughness transform should preserve offset x");
    require_close(material.clearcoat_roughness_texture.offset.y, 0.7F,
                  "clearcoat roughness transform should preserve offset y");
    require_close(material.clearcoat_roughness_texture.scale.x, 0.8F,
                  "clearcoat roughness transform should preserve scale x");
    require_close(material.clearcoat_roughness_texture.scale.y, 0.9F,
                  "clearcoat roughness transform should preserve scale y");
    require_close(material.clearcoat_normal_texture.offset.x, 0.1F,
                  "clearcoat normal transform should preserve offset x");
    require_close(material.clearcoat_normal_texture.offset.y, 0.9F,
                  "clearcoat normal transform should preserve offset y");
    require_close(material.clearcoat_normal_texture.rotation, 0.25F,
                  "clearcoat normal transform should preserve rotation");
    require_close(material.clearcoat_normal_scale, 0.8F,
                  "clearcoat normal texture should preserve its authored scale");

    const auto require_invalid = [&path](std::string_view field, std::string_view value) {
        write_text_file(path,
                        std::string{"{\n  \"asset\": {\"version\": \"2.0\"},\n"} +
                            "  \"extensionsUsed\": [\"KHR_materials_clearcoat\"],\n" +
                            "  \"materials\": [{\"extensions\": {\"KHR_materials_clearcoat\": {\"" +
                            std::string{field} + "\": " + std::string{value} + "}}}]\n}\n");
        require_throws_with_message([&path] { (void)cubey::asset::load_gltf_asset(path); }, field,
                                    "invalid clearcoat factor should identify its rejected field");
    };
    require_invalid("clearcoatFactor", "-0.01");
    require_invalid("clearcoatFactor", "1.01");
    require_invalid("clearcoatFactor", "1e999");
    require_invalid("clearcoatRoughnessFactor", "-0.01");
    require_invalid("clearcoatRoughnessFactor", "1.01");
    require_invalid("clearcoatRoughnessFactor", "1e999");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_clearcoat", "KHR_materials_unlit"],
  "extensionsRequired": ["KHR_materials_clearcoat", "KHR_materials_unlit"],
  "materials": [{"extensions": {
    "KHR_materials_clearcoat": {"clearcoatFactor": 0.5},
    "KHR_materials_unlit": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_clearcoat",
        "clearcoat and unlit should be rejected as an invalid material combination");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_clearcoat",
    "KHR_materials_pbrSpecularGlossiness"
  ],
  "materials": [{"extensions": {
    "KHR_materials_clearcoat": {"clearcoatFactor": 0.5},
    "KHR_materials_pbrSpecularGlossiness": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_clearcoat",
        "clearcoat and specular-glossiness should be rejected as an invalid material combination");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_closes_transmission_material_contract() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_transmission_contract");
    const std::filesystem::path path = dir / "transmission_contract.gltf";
    constexpr std::string_view kTinyPng =
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
        "x8AAwMCAO+/p9sAAAAASUVORK5CYII=";

    write_text_file(path, std::string(R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_transmission", "KHR_texture_transform"],
  "extensionsRequired": ["KHR_materials_transmission", "KHR_texture_transform"],
  "materials": [
    {"extensions": {"KHR_materials_transmission": {}}},
    {"extensions": {"KHR_materials_transmission": {
      "transmissionFactor": 0.6,
      "transmissionTexture": {
        "index": 0, "texCoord": 1,
        "extensions": {"KHR_texture_transform": {
          "offset": [0.2, 0.3], "rotation": 0.5, "scale": [0.4, 0.5], "texCoord": 0
        }}
      }
    }}}
  ],
  "textures": [{"source": 0}],
  "images": [{"uri": ")JSON") +
                              std::string{kTinyPng} + R"JSON("}]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);
    require(asset.materials.size() == 3,
            "transmission fixture should retain its implicit and authored materials");
    const cubey::asset::GltfMaterial& defaults = asset.materials[1];
    require_close(defaults.transmission_factor, 0.0F,
                  "transmissionFactor should default to a neutral zero factor");
    require(!defaults.transmission_texture.has_value(),
            "a default transmission extension should not invent a texture reference");

    const cubey::asset::GltfMaterial& material = asset.materials[2];
    require_close(material.transmission_factor, 0.6F,
                  "transmissionFactor should preserve its authored linear multiplier");
    require(material.transmission_texture.texture_index == 0,
            "transmission texture should preserve its source texture");
    require(material.transmission_texture.texcoord == 0,
            "transmission texture transform should override the source texture coordinate set");
    require_close(material.transmission_texture.offset.x, 0.2F,
                  "transmission texture transform should preserve offset x");
    require_close(material.transmission_texture.offset.y, 0.3F,
                  "transmission texture transform should preserve offset y");
    require_close(material.transmission_texture.rotation, 0.5F,
                  "transmission texture transform should preserve rotation");
    require_close(material.transmission_texture.scale.x, 0.4F,
                  "transmission texture transform should preserve scale x");
    require_close(material.transmission_texture.scale.y, 0.5F,
                  "transmission texture transform should preserve scale y");

    const auto require_invalid = [&path](std::string_view value) {
        write_text_file(path,
                        std::string{"{\n  \"asset\": {\"version\": \"2.0\"},\n"} +
                            "  \"extensionsUsed\": [\"KHR_materials_transmission\"],\n" +
                            "  \"materials\": [{\"extensions\": {\"KHR_materials_transmission\": "
                            "{\"transmissionFactor\": " +
                            std::string{value} + "}}}]\n}\n");
        require_throws_with_message(
            [&path] { (void)cubey::asset::load_gltf_asset(path); }, "transmissionFactor",
            "invalid transmission factor should identify its rejected field");
    };
    require_invalid("-0.01");
    require_invalid("1.01");
    require_invalid("1e999");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_transmission", "KHR_materials_unlit"],
  "materials": [{"extensions": {
    "KHR_materials_transmission": {"transmissionFactor": 0.5},
    "KHR_materials_unlit": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_transmission",
        "transmission and unlit should be rejected as an invalid material combination");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_transmission",
    "KHR_materials_pbrSpecularGlossiness"
  ],
  "materials": [{"extensions": {
    "KHR_materials_transmission": {"transmissionFactor": 0.5},
    "KHR_materials_pbrSpecularGlossiness": {}
  }}]
})JSON");
    require_throws_with_message([&path] { (void)cubey::asset::load_gltf_asset(path); },
                                "KHR_materials_transmission",
                                "transmission and specular-glossiness should be rejected as an "
                                "invalid material combination");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_closes_volume_material_contract() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_volume_contract");
    const std::filesystem::path path = dir / "volume_contract.gltf";
    constexpr std::string_view kTinyPng =
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
        "x8AAwMCAO+/p9sAAAAASUVORK5CYII=";

    write_text_file(path, std::string(R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_transmission", "KHR_materials_volume", "KHR_texture_transform"
  ],
  "extensionsRequired": ["KHR_materials_transmission", "KHR_materials_volume"],
  "materials": [
    {"extensions": {
      "KHR_materials_transmission": {"transmissionFactor": 0.8},
      "KHR_materials_volume": {}
    }},
    {"extensions": {
      "KHR_materials_transmission": {"transmissionFactor": 0.6},
      "KHR_materials_volume": {
        "thicknessFactor": 0.75,
        "thicknessTexture": {"index": 0, "texCoord": 1, "extensions": {
          "KHR_texture_transform": {
            "offset": [0.2, 0.3], "rotation": 0.5, "scale": [0.4, 0.5], "texCoord": 0
          }
        }},
        "attenuationColor": [0.2, 0.4, 0.6],
        "attenuationDistance": 2.5
      }
    }}
  ],
  "textures": [{"source": 0}],
  "images": [{"uri": ")JSON") +
                              std::string{kTinyPng} + R"JSON("}]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);
    require(asset.materials.size() == 3,
            "volume fixture should retain its implicit and authored materials");
    const cubey::asset::GltfMaterial& defaults = asset.materials[1];
    require_close(defaults.volume_thickness_factor, 0.0F,
                  "volume thickness should default to a neutral thin-wall path");
    require(!defaults.volume_thickness_texture.has_value(),
            "default volume should not invent a thickness texture reference");
    require_close(defaults.volume_attenuation_color.r, 1.0F,
                  "volume attenuation color should default to no attenuation");
    require_close(defaults.volume_attenuation_distance, 0.0F,
                  "volume default attenuation distance should use the internal infinite sentinel");

    const cubey::asset::GltfMaterial& material = asset.materials[2];
    require_close(material.volume_thickness_factor, 0.75F,
                  "volume thickness factor should preserve the authored mesh-space multiplier");
    require(material.volume_thickness_texture.texture_index == 0,
            "volume thickness texture should preserve its source texture");
    require(material.volume_thickness_texture.texcoord == 0,
            "volume thickness transform should independently override the UV set");
    require_close(material.volume_thickness_texture.offset.x, 0.2F,
                  "volume thickness transform should preserve offset x");
    require_close(material.volume_thickness_texture.offset.y, 0.3F,
                  "volume thickness transform should preserve offset y");
    require_close(material.volume_thickness_texture.rotation, 0.5F,
                  "volume thickness transform should preserve rotation");
    require_close(material.volume_thickness_texture.scale.x, 0.4F,
                  "volume thickness transform should preserve scale x");
    require_close(material.volume_thickness_texture.scale.y, 0.5F,
                  "volume thickness transform should preserve scale y");
    require_close(material.volume_attenuation_color.r, 0.2F,
                  "volume attenuation color should preserve red");
    require_close(material.volume_attenuation_color.g, 0.4F,
                  "volume attenuation color should preserve green");
    require_close(material.volume_attenuation_color.b, 0.6F,
                  "volume attenuation color should preserve blue");
    require_close(material.volume_attenuation_distance, 2.5F,
                  "volume attenuation distance should preserve its world-space authored value");

    const auto require_invalid = [&path](std::string_view extension_json,
                                         std::string_view expected_field) {
        write_text_file(path, std::string{"{\n  \"asset\": {\"version\": \"2.0\"},\n"} +
                                  "  \"extensionsUsed\": [\"KHR_materials_transmission\", "
                                  "\"KHR_materials_volume\"],\n"
                                  "  \"materials\": [{\"extensions\": {"
                                  "\"KHR_materials_transmission\": {\"transmissionFactor\": 0.5}, "
                                  "\"KHR_materials_volume\": " +
                                  std::string{extension_json} + "}}]\n}\n");
        require_throws_with_message(
            [&path] { (void)cubey::asset::load_gltf_asset(path); }, expected_field,
            "invalid volume material field should identify its rejected field");
    };
    require_invalid("{\"thicknessFactor\": -0.01}", "thicknessFactor");
    require_invalid("{\"thicknessFactor\": 1e999}", "thicknessFactor");
    require_invalid("{\"attenuationColor\": [1.1, 0.5, 0.5]}", "attenuationColor");
    require_invalid("{\"attenuationColor\": [0.5, 1e999, 0.5]}", "attenuationColor");
    require_invalid("{\"attenuationDistance\": 0}", "attenuationDistance");
    require_invalid("{\"attenuationDistance\": 1e999}", "attenuationDistance");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_volume"],
  "materials": [{"extensions": {"KHR_materials_volume": {"thicknessFactor": 0.5}}}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); },
        "requires KHR_materials_transmission",
        "volume should reject a material missing its required transmission extension");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_unlit"],
  "materials": [{"extensions": {
    "KHR_materials_transmission": {"transmissionFactor": 0.5},
    "KHR_materials_volume": {"thicknessFactor": 0.5},
    "KHR_materials_unlit": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_volume",
        "volume and unlit should be rejected as an invalid material combination");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_pbrSpecularGlossiness"
  ],
  "materials": [{"extensions": {
    "KHR_materials_transmission": {"transmissionFactor": 0.5},
    "KHR_materials_volume": {"thicknessFactor": 0.5},
    "KHR_materials_pbrSpecularGlossiness": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_volume",
        "volume and specular-glossiness should be rejected as an invalid material combination");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_closes_dispersion_material_contract() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_dispersion_contract");
    const std::filesystem::path path = dir / "dispersion_contract.gltf";

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_dispersion"
  ],
  "extensionsRequired": [
    "KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_dispersion"
  ],
  "materials": [
    {"extensions": {
      "KHR_materials_transmission": {"transmissionFactor": 0.8},
      "KHR_materials_volume": {"thicknessFactor": 0.5},
      "KHR_materials_dispersion": {}
    }},
    {"extensions": {
      "KHR_materials_transmission": {"transmissionFactor": 0.8},
      "KHR_materials_volume": {"thicknessFactor": 0.5},
      "KHR_materials_dispersion": {"dispersion": 2.04}
    }}
  ]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);
    require(asset.materials.size() == 3,
            "dispersion fixture should retain its implicit and authored materials");
    require_close(asset.materials[1].dispersion, 0.0F,
                  "dispersion should default to a neutral zero factor");
    require_close(asset.materials[2].dispersion, 2.04F,
                  "dispersion should preserve an authored value without upper clipping");

    const auto require_invalid = [&path](std::string_view value) {
        write_text_file(path, std::string{"{\n  \"asset\": {\"version\": \"2.0\"},\n"} +
                                  "  \"extensionsUsed\": [\"KHR_materials_transmission\", "
                                  "\"KHR_materials_volume\", \"KHR_materials_dispersion\"],\n"
                                  "  \"materials\": [{\"extensions\": {"
                                  "\"KHR_materials_transmission\": {\"transmissionFactor\": 0.5}, "
                                  "\"KHR_materials_volume\": {\"thicknessFactor\": 0.5}, "
                                  "\"KHR_materials_dispersion\": {\"dispersion\": " +
                                  std::string{value} + "}}}]\n}\n");
        require_throws_with_message([&path] { (void)cubey::asset::load_gltf_asset(path); },
                                    "dispersion",
                                    "invalid dispersion should identify its rejected field");
    };
    require_invalid("-0.01");
    require_invalid("1e999");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_transmission", "KHR_materials_dispersion"],
  "materials": [{"extensions": {
    "KHR_materials_transmission": {"transmissionFactor": 0.5},
    "KHR_materials_dispersion": {"dispersion": 0.5}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "requires KHR_materials_volume",
        "dispersion should reject a material missing its required volume extension");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_dispersion",
    "KHR_materials_unlit"
  ],
  "materials": [{"extensions": {
    "KHR_materials_transmission": {"transmissionFactor": 0.5},
    "KHR_materials_volume": {"thicknessFactor": 0.5},
    "KHR_materials_dispersion": {"dispersion": 0.5},
    "KHR_materials_unlit": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_dispersion",
        "dispersion and unlit should be rejected as an invalid material combination");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_transmission", "KHR_materials_volume", "KHR_materials_dispersion",
    "KHR_materials_pbrSpecularGlossiness"
  ],
  "materials": [{"extensions": {
    "KHR_materials_transmission": {"transmissionFactor": 0.5},
    "KHR_materials_volume": {"thicknessFactor": 0.5},
    "KHR_materials_dispersion": {"dispersion": 0.5},
    "KHR_materials_pbrSpecularGlossiness": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_dispersion",
        "dispersion and specular-glossiness should be rejected as an invalid combination");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_closes_sheen_material_contract() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_sheen_contract");
    const std::filesystem::path path = dir / "sheen_contract.gltf";
    constexpr std::string_view kTinyPng =
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
        "x8AAwMCAO+/p9sAAAAASUVORK5CYII=";

    write_text_file(path, std::string(R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_sheen", "KHR_texture_transform"],
  "extensionsRequired": ["KHR_materials_sheen", "KHR_texture_transform"],
  "materials": [
    {"extensions": {"KHR_materials_sheen": {}}},
    {"extensions": {"KHR_materials_sheen": {
      "sheenColorFactor": [0.2, 0.3, 0.4],
      "sheenColorTexture": {"index": 0, "texCoord": 1, "extensions": {
        "KHR_texture_transform": {
          "offset": [0.2, 0.3], "rotation": 0.5, "scale": [0.4, 0.5], "texCoord": 0
        }
      }},
      "sheenRoughnessFactor": 0.65,
      "sheenRoughnessTexture": {"index": 1, "extensions": {
        "KHR_texture_transform": {"offset": [0.6, 0.7], "scale": [0.8, 0.9]}
      }}
    }}}
  ],
  "textures": [{"source": 0}, {"source": 0}],
  "images": [{"uri": ")JSON") +
                              std::string{kTinyPng} + R"JSON("}]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);
    require(asset.materials.size() == 3,
            "required sheen fixture should retain implicit and authored materials");
    const cubey::asset::GltfMaterial& defaults = asset.materials[1];
    require_close(defaults.sheen_color_factor.r, 0.0F,
                  "sheenColorFactor should default to a disabled layer");
    require_close(defaults.sheen_roughness_factor, 0.0F,
                  "sheenRoughnessFactor should preserve the glTF default");
    require(!defaults.sheen_color_texture.has_value() &&
                !defaults.sheen_roughness_texture.has_value(),
            "default sheen material should not invent texture references");

    const cubey::asset::GltfMaterial& material = asset.materials[2];
    require_close(material.sheen_color_factor.r, 0.2F,
                  "sheen color red should preserve its authored factor");
    require_close(material.sheen_color_factor.g, 0.3F,
                  "sheen color green should preserve its authored factor");
    require_close(material.sheen_color_factor.b, 0.4F,
                  "sheen color blue should preserve its authored factor");
    require_close(material.sheen_roughness_factor, 0.65F,
                  "sheen roughness should preserve its authored factor");
    require(material.sheen_color_texture.texture_index == 0 &&
                material.sheen_roughness_texture.texture_index == 1,
            "sheen channels should preserve their independent source textures");
    require(material.sheen_color_texture.texcoord == 0,
            "sheen color transform should override the source texture coordinate set");
    require_close(material.sheen_color_texture.offset.x, 0.2F,
                  "sheen color transform should preserve offset x");
    require_close(material.sheen_color_texture.rotation, 0.5F,
                  "sheen color transform should preserve rotation");
    require_close(material.sheen_roughness_texture.offset.y, 0.7F,
                  "sheen roughness transform should preserve offset y");
    require_close(material.sheen_roughness_texture.scale.x, 0.8F,
                  "sheen roughness transform should preserve scale x");

    const auto require_invalid = [&path](std::string_view field, std::string_view value) {
        write_text_file(path, std::string{"{\n  \"asset\": {\"version\": \"2.0\"},\n"} +
                                  "  \"extensionsUsed\": [\"KHR_materials_sheen\"],\n" +
                                  "  \"materials\": [{\"extensions\": "
                                  "{\"KHR_materials_sheen\": {\"" +
                                  std::string{field} + "\": " + std::string{value} + "}}}]\n}\n");
        require_throws_with_message([&path] { (void)cubey::asset::load_gltf_asset(path); }, field,
                                    "invalid sheen field should identify its rejected field");
    };
    require_invalid("sheenColorFactor", "[-0.01, 0.0, 0.0]");
    require_invalid("sheenColorFactor", "[0.0, 1.01, 0.0]");
    require_invalid("sheenColorFactor", "[0.0, 0.0, 1e999]");
    require_invalid("sheenRoughnessFactor", "-0.01");
    require_invalid("sheenRoughnessFactor", "1.01");
    require_invalid("sheenRoughnessFactor", "1e999");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_sheen", "KHR_materials_unlit"],
  "materials": [{"extensions": {
    "KHR_materials_sheen": {},
    "KHR_materials_unlit": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_sheen",
        "sheen and unlit should be rejected as an invalid material combination");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_sheen",
    "KHR_materials_pbrSpecularGlossiness"
  ],
  "materials": [{"extensions": {
    "KHR_materials_sheen": {},
    "KHR_materials_pbrSpecularGlossiness": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_sheen",
        "sheen and specular-glossiness should be rejected as an invalid material combination");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_closes_anisotropy_material_contract() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_anisotropy_contract");
    const std::filesystem::path path = dir / "anisotropy_contract.gltf";

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_anisotropy"],
  "extensionsRequired": ["KHR_materials_anisotropy"],
  "materials": [{"extensions": {"KHR_materials_anisotropy": {
    "anisotropyStrength": 0.55,
    "anisotropyRotation": 1.25
  }}}]
})JSON");
    const cubey::asset::GltfAsset valid = cubey::asset::load_gltf_asset(path);
    require(valid.materials.size() == 2,
            "required anisotropy material should load after renderer closure");
    require_close(valid.materials[1].anisotropy_strength, 0.55F,
                  "anisotropy strength should preserve its linear factor");
    require_close(valid.materials[1].anisotropy_rotation, 1.25F,
                  "anisotropy rotation should preserve its radian value");

    const auto require_invalid = [&path](std::string_view field, std::string_view value) {
        write_text_file(path, std::string{"{\n  \"asset\": {\"version\": \"2.0\"},\n"} +
                                  "  \"extensionsUsed\": [\"KHR_materials_anisotropy\"],\n" +
                                  "  \"materials\": [{\"extensions\": "
                                  "{\"KHR_materials_anisotropy\": {\"" +
                                  std::string{field} + "\": " + std::string{value} + "}}}]\n}\n");
        require_throws_with_message([&path] { (void)cubey::asset::load_gltf_asset(path); }, field,
                                    "invalid anisotropy field should identify its rejected field");
    };
    require_invalid("anisotropyStrength", "-0.01");
    require_invalid("anisotropyStrength", "1.01");
    require_invalid("anisotropyStrength", "1e999");
    require_invalid("anisotropyRotation", "1e999");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_anisotropy", "KHR_materials_unlit"],
  "materials": [{"extensions": {
    "KHR_materials_anisotropy": {},
    "KHR_materials_unlit": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_anisotropy",
        "anisotropy and unlit should be rejected as an invalid material combination");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_anisotropy",
    "KHR_materials_pbrSpecularGlossiness"
  ],
  "materials": [{"extensions": {
    "KHR_materials_anisotropy": {},
    "KHR_materials_pbrSpecularGlossiness": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_anisotropy",
        "anisotropy and specular-glossiness should be rejected as an invalid material combination");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_closes_iridescence_material_contract() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_iridescence_contract");
    const std::filesystem::path path = dir / "iridescence_contract.gltf";
    constexpr std::string_view kTinyPng =
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/"
        "x8AAwMCAO+/p9sAAAAASUVORK5CYII=";

    write_text_file(path, std::string(R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_iridescence", "KHR_texture_transform"],
  "extensionsRequired": ["KHR_materials_iridescence", "KHR_texture_transform"],
  "materials": [
    {"extensions": {"KHR_materials_iridescence": {}}},
    {"extensions": {"KHR_materials_iridescence": {
      "iridescenceFactor": 0.65,
      "iridescenceTexture": {"index": 0, "texCoord": 1, "extensions": {
        "KHR_texture_transform": {
          "offset": [0.2, 0.3], "rotation": 0.5, "scale": [0.4, 0.5], "texCoord": 0
        }
      }},
      "iridescenceIor": 1.4,
      "iridescenceThicknessMinimum": 520.0,
      "iridescenceThicknessMaximum": 120.0,
      "iridescenceThicknessTexture": {"index": 1, "extensions": {
        "KHR_texture_transform": {"offset": [0.6, 0.7], "scale": [0.8, 0.9]}
      }}
    }}}
  ],
  "textures": [{"source": 0}, {"source": 0}],
  "images": [{"uri": ")JSON") +
                              std::string{kTinyPng} + R"JSON("}]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);
    require(asset.materials.size() == 3,
            "required iridescence fixture should retain implicit and authored materials");
    const cubey::asset::GltfMaterial& defaults = asset.materials[1];
    require_close(defaults.iridescence_factor, 0.0F,
                  "iridescenceFactor should default to a disabled interface");
    require_close(defaults.iridescence_ior, 1.3F,
                  "iridescenceIor should preserve the glTF default");
    require_close(defaults.iridescence_thickness_minimum, 100.0F,
                  "iridescence thickness minimum should preserve the glTF default");
    require_close(defaults.iridescence_thickness_maximum, 400.0F,
                  "iridescence thickness maximum should preserve the glTF default");
    require(!defaults.iridescence_texture.has_value() &&
                !defaults.iridescence_thickness_texture.has_value(),
            "default iridescence material should not invent texture references");

    const cubey::asset::GltfMaterial& material = asset.materials[2];
    require_close(material.iridescence_factor, 0.65F,
                  "iridescenceFactor should preserve its linear multiplier");
    require_close(material.iridescence_ior, 1.4F,
                  "iridescenceIor should preserve its authored value");
    require_close(material.iridescence_thickness_minimum, 520.0F,
                  "iridescence thickness minimum should preserve authored values");
    require_close(material.iridescence_thickness_maximum, 120.0F,
                  "iridescence thickness maximum should preserve authored values");
    require(material.iridescence_texture.texture_index == 0 &&
                material.iridescence_thickness_texture.texture_index == 1,
            "iridescence channels should preserve their independent source textures");
    require(material.iridescence_texture.texcoord == 0,
            "iridescence texture transform should override the source texture coordinate set");
    require_close(material.iridescence_texture.offset.x, 0.2F,
                  "iridescence texture transform should preserve offset x");
    require_close(material.iridescence_texture.rotation, 0.5F,
                  "iridescence texture transform should preserve rotation");
    require_close(material.iridescence_thickness_texture.offset.y, 0.7F,
                  "iridescence thickness transform should preserve offset y");
    require_close(material.iridescence_thickness_texture.scale.x, 0.8F,
                  "iridescence thickness transform should preserve scale x");

    const auto require_invalid = [&path](std::string_view field, std::string_view value) {
        write_text_file(
            path, std::string{"{\n  \"asset\": {\"version\": \"2.0\"},\n"} +
                      "  \"extensionsUsed\": [\"KHR_materials_iridescence\"],\n" +
                      "  \"materials\": [{\"extensions\": {\"KHR_materials_iridescence\": {\"" +
                      std::string{field} + "\": " + std::string{value} + "}}}]\n}\n");
        require_throws_with_message([&path] { (void)cubey::asset::load_gltf_asset(path); }, field,
                                    "invalid iridescence field should identify its rejected field");
    };
    require_invalid("iridescenceFactor", "-0.01");
    require_invalid("iridescenceFactor", "1.01");
    require_invalid("iridescenceFactor", "1e999");
    require_invalid("iridescenceIor", "0.99");
    require_invalid("iridescenceIor", "1e999");
    require_invalid("iridescenceThicknessMinimum", "-0.01");
    require_invalid("iridescenceThicknessMinimum", "1e999");
    require_invalid("iridescenceThicknessMaximum", "-0.01");
    require_invalid("iridescenceThicknessMaximum", "1e999");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_materials_iridescence", "KHR_materials_unlit"],
  "materials": [{"extensions": {
    "KHR_materials_iridescence": {},
    "KHR_materials_unlit": {}
  }}]
})JSON");
    require_throws_with_message(
        [&path] { (void)cubey::asset::load_gltf_asset(path); }, "KHR_materials_iridescence",
        "iridescence and unlit should be rejected as an invalid material combination");

    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": [
    "KHR_materials_iridescence",
    "KHR_materials_pbrSpecularGlossiness"
  ],
  "materials": [{"extensions": {
    "KHR_materials_iridescence": {},
    "KHR_materials_pbrSpecularGlossiness": {}
  }}]
})JSON");
    require_throws_with_message([&path] { (void)cubey::asset::load_gltf_asset(path); },
                                "KHR_materials_iridescence",
                                "iridescence and specular-glossiness should be rejected as an "
                                "invalid material combination");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_enforces_anisotropy_tangent_space_contract() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_anisotropy_tangent_contract");
    const std::filesystem::path generated_uv1 =
        write_anisotropy_tangent_contract_gltf(dir, false, true, true, false, 0U, true, 1U);
    const cubey::asset::GltfAsset generated_asset = cubey::asset::load_gltf_asset(generated_uv1);
    const cubey::asset::GltfMeshPrimitive& generated = generated_asset.meshes[0].primitives[0];
    require_close(
        generated.vertices[0].tangent.x, 0.0F,
        "anisotropy tangent generation should not use TEXCOORD_0 when the texture selects UV1");
    require_close(
        generated.vertices[0].tangent.y, 1.0F,
        "anisotropy tangent generation should use the effective anisotropy texture UV set");

    cubey::asset::GltfLoadConfig disabled_generation;
    disabled_generation.generate_missing_tangents = false;
    require_throws_with_message(
        [&generated_uv1, disabled_generation] {
            static_cast<void>(cubey::asset::load_gltf_asset(generated_uv1, disabled_generation));
        },
        "KHR_materials_anisotropy",
        "anisotropy should reject absent TANGENT when generation is disabled");

    const std::filesystem::path missing_uv =
        write_anisotropy_tangent_contract_gltf(dir, false, false, false, false, 0U, false, 0U);
    require_throws_with_message(
        [&missing_uv] { static_cast<void>(cubey::asset::load_gltf_asset(missing_uv)); },
        "TEXCOORD_0",
        "anisotropy should reject generated tangent space without its required texture "
        "coordinates");

    const std::filesystem::path mismatched_generated =
        write_anisotropy_tangent_contract_gltf(dir, false, true, true, true, 0U, true, 1U);
    require_throws_with_message(
        [&mismatched_generated] {
            static_cast<void>(cubey::asset::load_gltf_asset(mismatched_generated));
        },
        "matching normalTexture and anisotropyTexture texCoord",
        "anisotropy should reject generated tangent frames that cannot satisfy both texture UV "
        "sets");

    const std::filesystem::path authored_tangent =
        write_anisotropy_tangent_contract_gltf(dir, true, true, true, true, 0U, true, 1U);
    const cubey::asset::GltfAsset authored_asset =
        cubey::asset::load_gltf_asset(authored_tangent, disabled_generation);
    require_close(authored_asset.meshes[0].primitives[0].vertices[0].tangent.x, 1.0F,
                  "authored tangents should satisfy anisotropy even when generation is disabled");

    std::filesystem::remove_all(dir);
}
