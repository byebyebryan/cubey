#include "fluid_3d_config.h"
#include "fluid_3d_injectors.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open " + path.string());
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

[[nodiscard]] float length_squared(std::array<float, 3> value) {
    return (value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]);
}

} // namespace

int main() {
    try {
        const cubey::projects::fluid_3d::Fluid3DConfig config;
        constexpr std::size_t kExpectedCellCount =
            std::size_t{128} * std::size_t{128} * std::size_t{128};
        require(config.grid_width == 128, "fluid 3D should default to 128 columns");
        require(config.grid_height == 128, "fluid 3D should default to 128 rows");
        require(config.grid_depth == 128, "fluid 3D should default to 128 slices");
        require(config.injector_count == 4, "fluid 3D should default to four injectors");
        require(config.pressure_iterations == 12,
                "fluid 3D pressure solve should default to a small 3D budget");
        require(config.raymarch_steps == 128, "fluid 3D should default to one step per slice");
        require(config.density_decay_per_second == 0.985F,
                "fluid 3D should default to enough dye decay to avoid filled-volume buildup");
        require(config.injector_strength == 6.0F,
                "fluid 3D injector force should match the shared CLI default");
        require(config.shadow_absorption == 48.0F,
                "fluid 3D should default to visible volume self-shadowing");
        require(config.ambient_light == 0.22F,
                "fluid 3D should default to a small amount of ambient volume light");
        require(cubey::projects::fluid_3d::volume_cell_count(config) == kExpectedCellCount,
                "fluid 3D cell count should multiply all dimensions");
        require(cubey::projects::fluid_3d::volume_rgba32f_byte_size(config) ==
                    sizeof(float) * 4U * kExpectedCellCount,
                "fluid 3D RGBA32F byte size should cover one texel per cell");
        require(cubey::projects::fluid_3d::next_debug_view(
                    cubey::projects::fluid_3d::Fluid3DDebugView::Smoke) ==
                    cubey::projects::fluid_3d::Fluid3DDebugView::DensitySlice,
                "fluid 3D debug view should cycle from smoke to density slice");
        require(cubey::projects::fluid_3d::next_debug_view(
                    cubey::projects::fluid_3d::Fluid3DDebugView::DensitySlice) ==
                    cubey::projects::fluid_3d::Fluid3DDebugView::Velocity,
                "fluid 3D debug view should cycle from density slice to velocity");
        require(cubey::projects::fluid_3d::next_debug_view(
                    cubey::projects::fluid_3d::Fluid3DDebugView::Velocity) ==
                    cubey::projects::fluid_3d::Fluid3DDebugView::Smoke,
                "fluid 3D debug view should cycle from velocity to smoke");

        cubey::RunConfig run_config;
        run_config.grid_width = 64;
        run_config.grid_height = 48;
        run_config.grid_depth = 32;
        run_config.injectors = 8;
        run_config.injector_force = 7.5F;
        const cubey::projects::fluid_3d::Fluid3DConfig configured =
            cubey::projects::fluid_3d::fluid_3d_config_from_run_config(run_config);
        require(configured.grid_width == 64, "fluid 3D config should honor run config width");
        require(configured.grid_height == 48, "fluid 3D config should honor run config height");
        require(configured.grid_depth == 32, "fluid 3D config should honor run config depth");
        require(configured.injector_count == 8,
                "fluid 3D config should honor run config injector count");
        require(configured.injector_strength == 7.5F,
                "fluid 3D config should honor run config injector force");

        bool threw_for_too_many_injectors = false;
        try {
            cubey::RunConfig invalid_injector_config;
            invalid_injector_config.injectors =
                cubey::projects::fluid_3d::kMaxFluid3DInjectorCount + 1U;
            static_cast<void>(cubey::projects::fluid_3d::fluid_3d_config_from_run_config(
                invalid_injector_config));
        } catch (const std::runtime_error&) {
            threw_for_too_many_injectors = true;
        }
        require(threw_for_too_many_injectors,
                "fluid 3D config should reject injector counts above the shader policy limit");

        std::vector<cubey::projects::fluid_3d::Fluid3DInjectorState> injectors =
            cubey::projects::fluid_3d::create_fluid_3d_injectors(configured);
        require(injectors.size() == 8, "fluid 3D injector state should match configured count");
        require(injectors.front().radius < injectors.back().radius,
                "fluid 3D injector radii should spread through the volume");
        require(length_squared(injectors.front().color) > 0.0F,
                "fluid 3D injectors should carry display color");
        const std::array<float, 3> initial_position = injectors.front().position;
        std::vector<cubey::projects::fluid_3d::Fluid3DInjectorGpu> gpu_injectors =
            cubey::projects::fluid_3d::fluid_3d_injectors_to_gpu(injectors, configured);
        require(gpu_injectors.size() == 8, "fluid 3D GPU injector state should match state count");
        require(gpu_injectors.front().position_radius[3] == configured.injector_radius,
                "fluid 3D GPU injector should carry configured radius");
        require(gpu_injectors.front().velocity_strength[3] == configured.injector_strength,
                "fluid 3D GPU injector should carry configured force");

        const cubey::FrameTiming timing{
            .delta_seconds = configured.fixed_delta_seconds,
            .elapsed_seconds = configured.fixed_delta_seconds * 12.0,
            .frame_index = 12,
        };
        gpu_injectors =
            cubey::projects::fluid_3d::update_fluid_3d_injectors(injectors, configured, timing);
        require(injectors.front().position != initial_position,
                "fluid 3D injector update should advance orbit position");
        require(length_squared(injectors.front().velocity) > 0.0F,
                "fluid 3D injector update should produce carry velocity");
        require(cubey::projects::fluid_3d::fluid_3d_injector_byte_size(configured) ==
                    sizeof(cubey::projects::fluid_3d::Fluid3DInjectorGpu) * 8U,
                "fluid 3D active injector byte size should match configured count");
        require(cubey::projects::fluid_3d::fluid_3d_injector_capacity_byte_size() ==
                    sizeof(cubey::projects::fluid_3d::Fluid3DInjectorGpu) *
                        cubey::projects::fluid_3d::kMaxFluid3DInjectorCount,
                "fluid 3D injector capacity byte size should cover the shader policy limit");

        require(cubey::projects::fluid_3d::fluid_3d_headless_frame_count(run_config) == 120,
                "fluid 3D headless PNG should default to a settled simulation frame");
        run_config.frames = 8;
        require(cubey::projects::fluid_3d::fluid_3d_headless_frame_count(run_config) == 8,
                "fluid 3D headless frame count should honor explicit frames");
        const cubey::FrameTiming fixed_timing =
            cubey::projects::fluid_3d::fixed_fluid_3d_headless_timing(configured, 5);
        require(fixed_timing.frame_index == 5,
                "fluid 3D fixed headless timing should preserve frame index");
        require(fixed_timing.delta_seconds == configured.fixed_delta_seconds,
                "fluid 3D fixed headless timing should use fixed dt");

        const std::filesystem::path source_dir = CUBEY_FLUID_3D_SOURCE_DIR;
        const std::string advect_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_advect.comp");
        const std::string advect_correct_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_advect_correct.comp");
        const std::string render_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_raymarch.frag");
        const std::string shadow_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_shadow.comp");
        require_contains(advect_shader, "layout(rgba32f",
                         "fluid 3D compute shaders should use explicit storage image formats");
        require_contains(advect_correct_shader, "reversed_density",
                         "fluid 3D should use a MacCormack/BFECC correction pass");
        require_contains(advect_correct_shader, "source_bounds",
                         "fluid 3D MacCormack correction should use a source-neighborhood limiter");
        require_contains(render_shader, "sampler3D",
                         "fluid 3D render shader should raymarch sampled 3D textures");
        require_contains(render_shader, "shadow_volume",
                         "fluid 3D render shader should sample the computed shadow volume");
        require_contains(shadow_shader, "light_transmittance",
                         "fluid 3D shadow shader should precompute volume light transmittance");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_3d_config_tests: %s\n", error.what());
        return 1;
    }
}
