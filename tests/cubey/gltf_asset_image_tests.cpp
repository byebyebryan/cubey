#include "gltf_asset_test_support.h"

using namespace cubey::test_support;

void test_gltf_asset_loads_required_basisu_texture_source() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_basisu_texture");
    cubey::write_binary_file(dir / "texture.ktx2", minimal_ktx2_header(8, 4, 3));
    const std::filesystem::path path = dir / "basisu_texture.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_texture_basisu"],
  "extensionsRequired": ["KHR_texture_basisu"],
  "textures": [{
    "source": 0,
    "extensions": {
      "KHR_texture_basisu": {"source": 1}
    }
  }],
  "images": [
    {
      "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII="
    },
    {
      "uri": "texture.ktx2",
      "mimeType": "image/ktx2"
    }
  ]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.images.size() == 2, "loader should preserve fallback and basisu images");
    require(asset.textures.size() == 1, "loader should preserve texture");
    require(asset.textures[0].image_index == 1,
            "KHR_texture_basisu should override the fallback texture source");
    require(asset.images[1].encoding == cubey::asset::GltfImageEncoding::Ktx2Basisu,
            "basisu image should stay encoded instead of being decoded by stb");
    require(asset.images[1].width == 8, "KTX2 image width should load from the header");
    require(asset.images[1].height == 4, "KTX2 image height should load from the header");
    require(asset.images[1].mip_levels == 3, "KTX2 image mip count should load from the header");
    require(!asset.images[1].encoded_bytes.empty(), "KTX2 bytes should be preserved");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_rejects_layered_basisu_material_images() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_basisu_layered_texture");
    cubey::write_binary_file(dir / "texture.ktx2", minimal_ktx2_header(8, 4, 3, 1));
    const std::filesystem::path path = dir / "basisu_layered_texture.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_texture_basisu"],
  "extensionsRequired": ["KHR_texture_basisu"],
  "textures": [{
    "extensions": {
      "KHR_texture_basisu": {"source": 0}
    }
  }],
  "images": [
    {
      "uri": "texture.ktx2",
      "mimeType": "image/ktx2"
    }
  ]
})JSON");

    require_throws([&path] { (void)cubey::asset::load_gltf_asset(path); },
                   "loader should reject layered KTX2 material textures");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_rejects_formatted_ktx2_basisu_sources() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_basisu_formatted_texture");
    std::vector<std::uint8_t> bytes = minimal_ktx2_header(8, 4, 1);
    bytes[12] = 37; // vkFormat must be undefined for KHR_texture_basisu payloads.
    cubey::write_binary_file(dir / "texture.ktx2", bytes);
    const std::filesystem::path path = dir / "basisu_formatted_texture.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "extensionsUsed": ["KHR_texture_basisu"],
  "extensionsRequired": ["KHR_texture_basisu"],
  "textures": [{
    "extensions": {
      "KHR_texture_basisu": {"source": 0}
    }
  }],
  "images": [
    {
      "uri": "texture.ktx2",
      "mimeType": "image/ktx2"
    }
  ]
})JSON");

    require_throws([&path] { (void)cubey::asset::load_gltf_asset(path); },
                   "loader should reject non-Basis formatted KTX2 textures");

    std::filesystem::remove_all(dir);
}

void test_gltf_asset_loads_sampler_mip_filter() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_sampler_mip_filter");
    const std::filesystem::path path = dir / "sampler_mip_filter.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "samplers": [
    {"minFilter": 9986, "magFilter": 9728, "wrapS": 33071, "wrapT": 33648},
    {"minFilter": 9729}
  ]
})JSON");

    const cubey::asset::GltfAsset asset = cubey::asset::load_gltf_asset(path);

    require(asset.samplers.size() == 2, "samplers should load");
    require(asset.samplers[0].min_filter == cubey::asset::GltfTextureFilter::Nearest,
            "NEAREST_MIPMAP_LINEAR should preserve nearest texel filtering");
    require(asset.samplers[0].mip_filter == cubey::asset::GltfTextureMipFilter::Linear,
            "NEAREST_MIPMAP_LINEAR should preserve linear mip filtering");
    require(asset.samplers[1].mip_filter == cubey::asset::GltfTextureMipFilter::None,
            "non-mipmapped min filters should disable mip sampling");

    std::filesystem::remove_all(dir);
}
