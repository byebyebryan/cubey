#include "gltf_asset_test_support.h"

using namespace cubey::test_support;

void test_gltf_asset_probes_transformed_scene_bounds_without_loading_buffers() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_bounds_probe");
    const std::filesystem::path path = dir / "bounds_probe.gltf";
    write_text_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}, {"nodes": [2]}],
  "nodes": [
    {"name": "Root", "translation": [10.0, 20.0, 30.0], "children": [1]},
    {"name": "ScaledMesh", "translation": [1.0, 2.0, 3.0],
     "scale": [2.0, 3.0, 4.0], "mesh": 0},
    {"name": "IgnoredSceneMesh", "translation": [1000.0, 1000.0, 1000.0], "mesh": 0}
  ],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
  "buffers": [{"uri": "never-read.bin", "byteLength": 36}],
  "bufferViews": [{"buffer": 0, "byteLength": 36}],
  "accessors": [{
    "bufferView": 0,
    "componentType": 5126,
    "count": 3,
    "type": "VEC3",
    "min": [-1.0, -2.0, -3.0],
    "max": [1.0, 2.0, 3.0]
  }]
})JSON");

    require(!std::filesystem::exists(dir / "never-read.bin"),
            "bounds probe fixture must not provide an external buffer");
    const cubey::asset::GltfBounds3D bounds = cubey::asset::probe_gltf_scene_bounds(path);
    require_close(bounds.center.x, 11.0F, "bounds probe should apply node translation on x");
    require_close(bounds.center.y, 22.0F, "bounds probe should apply node translation on y");
    require_close(bounds.center.z, 33.0F, "bounds probe should apply node translation on z");
    require_close(bounds.half_extent.x, 2.0F, "bounds probe should apply node scale on x");
    require_close(bounds.half_extent.y, 6.0F, "bounds probe should apply node scale on y");
    require_close(bounds.half_extent.z, 12.0F, "bounds probe should apply node scale on z");

    const cubey::asset::GltfBounds3D ignored_scene_bounds =
        cubey::asset::probe_gltf_scene_bounds(path, 1U);
    require_close(ignored_scene_bounds.center.x, 1000.0F,
                  "bounds probe should resolve an explicitly selected scene");

    const std::filesystem::path missing_bounds_path = dir / "missing_bounds.gltf";
    write_text_file(missing_bounds_path, R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
  "buffers": [{"uri": "never-read.bin", "byteLength": 36}],
  "bufferViews": [{"buffer": 0, "byteLength": 36}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}]
})JSON");
    require_throws_with_message(
        [&missing_bounds_path] {
            (void)cubey::asset::probe_gltf_scene_bounds(missing_bounds_path);
        },
        "min/max", "bounds probe should reject POSITION accessors without min/max metadata");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_probes_glb_json_without_reading_embedded_bin() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_glb_bounds_probe");
    const std::filesystem::path path = dir / "bounds_probe.glb";
    write_glb_file(path, R"JSON({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "EmbeddedBinMesh", "translation": [4.0, 5.0, 6.0], "mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
  "buffers": [{"byteLength": 36}],
  "bufferViews": [{"buffer": 0, "byteLength": 36}],
  "accessors": [{
    "bufferView": 0,
    "componentType": 5126,
    "count": 3,
    "type": "VEC3",
    "min": [-1.0, -2.0, -3.0],
    "max": [1.0, 2.0, 3.0]
  }]
})JSON",
                   36U);

    const cubey::asset::GltfBounds3D bounds = cubey::asset::probe_gltf_scene_bounds(path);
    require_close(bounds.center.x, 4.0F, "GLB bounds probe should apply node translation on x");
    require_close(bounds.center.y, 5.0F, "GLB bounds probe should apply node translation on y");
    require_close(bounds.center.z, 6.0F, "GLB bounds probe should apply node translation on z");
    require_close(bounds.half_extent.x, 1.0F, "GLB bounds probe should read JSON POSITION min/max");
    require_close(bounds.half_extent.y, 2.0F, "GLB bounds probe should read JSON POSITION min/max");
    require_close(bounds.half_extent.z, 3.0F, "GLB bounds probe should read JSON POSITION min/max");
    std::filesystem::remove_all(dir);
}

void test_gltf_asset_probe_rejects_malformed_glb_headers() {
    const std::filesystem::path dir = test_dir("cubey_gltf_asset_malformed_glb_probe");

    const std::filesystem::path truncated_path = dir / "truncated.glb";
    cubey::write_binary_file(truncated_path, std::vector<std::uint8_t>{0x67U, 0x6cU, 0x54U, 0x46U});
    require_throws_with_message(
        [&truncated_path] { (void)cubey::asset::probe_gltf_scene_bounds(truncated_path); },
        "GLB header", "bounds probe should reject a truncated GLB header");

    const std::filesystem::path version_path = dir / "version.glb";
    cubey::write_binary_file(version_path, make_glb_header(1U, 20U, 0U, 0x4E4F534AU));
    require_throws_with_message(
        [&version_path] { (void)cubey::asset::probe_gltf_scene_bounds(version_path); }, "version",
        "bounds probe should reject a GLB version other than two");

    const std::filesystem::path length_path = dir / "length.glb";
    cubey::write_binary_file(length_path, make_glb_header(2U, 28U, 0U, 0x4E4F534AU));
    require_throws_with_message(
        [&length_path] { (void)cubey::asset::probe_gltf_scene_bounds(length_path); },
        "declared length", "bounds probe should reject a GLB longer than the file");

    const std::filesystem::path chunk_path = dir / "chunk.glb";
    cubey::write_binary_file(chunk_path, make_glb_header(2U, 20U, 0U, 0x004E4942U));
    require_throws_with_message(
        [&chunk_path] { (void)cubey::asset::probe_gltf_scene_bounds(chunk_path); }, "JSON chunk",
        "bounds probe should reject a GLB without a first JSON chunk");

    std::filesystem::remove_all(dir);
}
