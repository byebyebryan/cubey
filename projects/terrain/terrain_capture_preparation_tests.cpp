#include "terrain_capture_preparation.h"
#include "terrain_config.h"
#include <chrono>
#include <cubey/asset/terrain_water_capture.h>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid capture preparation accepted");
}
} // namespace
int main() {
    try {
        using namespace cubey;
        using namespace cubey::projects::terrain;
        const auto root =
            std::filesystem::temp_directory_path() /
            ("cubey-capture-preparation-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{root};
        const std::filesystem::path source_path = CUBEY_TEST_HEIGHTFIELD;
        asset::TerrainRasterHeightSource source(source_path);
        asset::TerrainWaterCapture capture;
        capture.grid = {3, 3, 32, {-32, -32}};
        capture.source_elevation_sha256 = source.provenance().elevation_sha256;
        capture.recording_sha256 = capture.frame_sha256 = std::string(64, 'a');
        capture.rain_mm_per_hour = 512;
        capture.capture_time_s = 43200;
        for (std::uint32_t z = 0; z < 3; ++z)
            for (std::uint32_t x = 0; x < 3; ++x)
                capture.bed_m.push_back(
                    source.sample_height({.world_xz = {float(x) * 32 - 32, float(z) * 32 - 32}}));
        capture.depth_m.assign(9, 1);
        capture.velocity_xz_m_per_s.assign(9, math::Vec2{1, 0});
        asset::write_terrain_water_capture(root / "capture", capture);
        TerrainProjectConfig config;
        config.runtime.heightfield_path = source_path;
        config.water_capture_path = root / "capture/water-capture.json";
        config.captured_water_detail = "uniform";
        // Deliberately invalid *fixed mesh* budget. Uniform preparation must
        // not touch that mesh path or require a fixed-LOD budget.
        config.captured_water_vertex_budget = 1;
        auto uniform = prepare_terrain_capture(config, root / "unused-shaders");
        require(uniform->context_product() == nullptr, "context-off allocated exterior terrain");
        require(uniform->capture().depth_m == capture.depth_m, "preparation changed depth");
        uniform->set_shadow_strength(0);
        uniform->set_shadow_strength(1);
        rejects([&] { uniform->set_shadow_strength(-1); });
        rejects([&] { uniform->set_shadow_strength(std::numeric_limits<float>::quiet_NaN()); });
        auto shadow_config = config;
        shadow_config.captured_water_shadow_strength = 0;
        require(
            prepare_terrain_capture(shadow_config, root / "unused-shaders")->capture().depth_m ==
                capture.depth_m,
            "shadow ablation changed hydraulic depth");
        auto camera_config = config;
        camera_config.captured_water_focus_x_m = 16;
        camera_config.captured_water_focus_z_m = 16;
        (void)prepare_terrain_capture(camera_config, root / "unused-shaders");
        camera_config.captured_water_focus_x_m = 10000;
        rejects([&] { (void)prepare_terrain_capture(camera_config, root / "unused-shaders"); });
        camera_config.captured_water_focus_x_m = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { (void)prepare_terrain_capture(camera_config, root / "unused-shaders"); });
        camera_config.captured_water_focus_z_m.reset();
        rejects([&] { (void)prepare_terrain_capture(camera_config, root / "unused-shaders"); });
        auto fixed_config = config;
        fixed_config.captured_water_detail = "fixed";
        rejects([&] { (void)prepare_terrain_capture(fixed_config, root / "unused-shaders"); });
        fixed_config.captured_water_vertex_budget = 2000000;
        fixed_config.captured_water_detail_span_m = 32;
        fixed_config.captured_water_detail_focus_x_m = 16;
        fixed_config.captured_water_detail_focus_z_m = 16;
        auto fixed = prepare_terrain_capture(fixed_config, root / "unused-shaders");
        require(fixed->capture().bed_m == capture.bed_m, "fixed preparation changed bed");
        fixed_config.captured_water_detail_focus_x_m = 10000;
        rejects([&] { (void)prepare_terrain_capture(fixed_config, root / "unused-shaders"); });
        fixed_config.captured_water_detail_focus_z_m.reset();
        rejects([&] { (void)prepare_terrain_capture(fixed_config, root / "unused-shaders"); });
        config.captured_water_context = true;
        config.terrain_context_max_spacing_m = 512;
        auto context = prepare_terrain_capture(config, root / "unused-shaders");
        require(context->context_product() != nullptr, "context-on was ignored");
        std::cout << "terrain_capture_preparation: PASS uniform/fixed, budget, context, focus and "
                     "immutable fields\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
