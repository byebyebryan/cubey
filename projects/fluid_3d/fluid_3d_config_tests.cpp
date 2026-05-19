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
        require(config.shadow_grid_width == 64, "fluid 3D should default to a 64-wide shadow grid");
        require(config.shadow_grid_height == 64,
                "fluid 3D should default to a 64-high shadow grid");
        require(config.shadow_grid_depth == 64,
                "fluid 3D should default to a 64-deep shadow grid");
        require(config.shadow_steps == 64, "fluid 3D shadow rays should default to 64 steps");
        require(config.shadow_update_interval == 1,
                "fluid 3D should update shadows every frame by default");
        require(config.density_decay_per_second == 0.99F,
                "fluid 3D density decay should match the tuned default");
        require(config.velocity_decay_per_second == 0.99F,
                "fluid 3D velocity decay should match the tuned default");
        require(config.injector_radius == 0.05F,
                "fluid 3D injector radius should match the tuned default");
        require(config.injector_strength == 6.0F,
                "fluid 3D injector force should match the shared CLI default");
        require(config.injector_density_strength == 6.0F,
                "fluid 3D injector density injection should match the shared default");
        require(config.injector_propulsion_strength == 1.0F,
                "fluid 3D injector propulsion should default to neutral scaling");
        require(config.injector_orbit_radius == 0.25F,
                "fluid 3D injector orbit radius should default near volume center");
        require(config.injector_orbit_radius_spread == 0.22F,
                "fluid 3D injector orbit radius spread should default to a broad band");
        require(config.injector_orbit_angular_speed == 0.0F,
                "fluid 3D injector orbit speed should default to a centered signed band");
        require(config.injector_orbit_angular_speed_spread == 0.8F,
                "fluid 3D injector speed spread should default to mixed directions");
        require(config.injector_orbit_phase_spread == 1.0F,
                "fluid 3D injector phase spread should default around a full turn");
        require(config.injector_orbit_inclination_degrees == 0.0F,
                "fluid 3D injector inclination should default to horizontal orbits");
        require(config.injector_orbit_inclination_spread_degrees == 60.0F,
                "fluid 3D injector inclination spread should default to tilted 3D orbits");
        require(config.injector_movement ==
                    cubey::projects::fluid_3d::Fluid3DInjectorMovement::Orbit,
                "fluid 3D injector movement should default to orbit");
        require(config.injector_circle_height == 0.5F,
                "fluid 3D injector circle height should default to the volume center");
        require(config.vorticity_strength == 1.0F,
                "fluid 3D vorticity should match the tuned default");
        require(config.buoyancy_strength == 1.0F,
                "fluid 3D buoyancy should default to a mild upward force");
        require(config.absorption == 8.0F,
                "fluid 3D absorption should match the tuned default");
        require(config.emission == 2.0F,
                "fluid 3D light emission should match the tuned default");
        require(config.shadow_absorption == 50.0F,
                "fluid 3D shadow absorption should match the tuned default");
        require(config.ambient_light == 0.5F,
                "fluid 3D ambient light should match the tuned default");
        require(cubey::projects::fluid_3d::volume_cell_count(config) == kExpectedCellCount,
                "fluid 3D cell count should multiply all dimensions");
        require(cubey::projects::fluid_3d::volume_byte_size(config, 8) ==
                    8U * kExpectedCellCount,
                "fluid 3D generic byte size should use caller-provided cell bytes");
        require(cubey::projects::fluid_3d::shadow_volume_cell_count(config) ==
                    std::size_t{64} * std::size_t{64} * std::size_t{64},
                "fluid 3D shadow cell count should use the decoupled shadow grid");
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
        run_config.shadow_grid_width = 24;
        run_config.shadow_grid_height = 20;
        run_config.shadow_grid_depth = 16;
        run_config.shadow_steps = 48;
        run_config.shadow_update_interval = 3;
        run_config.injectors = 8;
        run_config.injector_force = 7.5F;
        run_config.injector_propulsion = 1.6F;
        run_config.fluid_density_injection = 6.5F;
        run_config.injector_orbit_radius = 0.24F;
        run_config.injector_orbit_radius_spread = 0.18F;
        run_config.injector_orbit_angular_speed = 0.1F;
        run_config.injector_orbit_angular_speed_spread = 1.2F;
        run_config.injector_orbit_phase_spread = 0.75F;
        run_config.injector_orbit_inclination_degrees = 10.0F;
        run_config.injector_orbit_inclination_spread_degrees = 50.0F;
        run_config.injector_movement = "circle";
        run_config.injector_circle_height = 0.65F;
        run_config.fluid_buoyancy = 1.75F;
        const cubey::projects::fluid_3d::Fluid3DConfig configured =
            cubey::projects::fluid_3d::fluid_3d_config_from_run_config(run_config);
        require(configured.grid_width == 64, "fluid 3D config should honor run config width");
        require(configured.grid_height == 48, "fluid 3D config should honor run config height");
        require(configured.grid_depth == 32, "fluid 3D config should honor run config depth");
        require(configured.shadow_grid_width == 24,
                "fluid 3D config should honor run config shadow width");
        require(configured.shadow_grid_height == 20,
                "fluid 3D config should honor run config shadow height");
        require(configured.shadow_grid_depth == 16,
                "fluid 3D config should honor run config shadow depth");
        require(configured.shadow_steps == 48,
                "fluid 3D config should honor run config shadow steps");
        require(configured.shadow_update_interval == 3,
                "fluid 3D config should honor run config shadow update interval");
        require(configured.injector_count == 8,
                "fluid 3D config should honor run config injector count");
        require(configured.injector_strength == 7.5F,
                "fluid 3D config should honor run config injector force");
        require(configured.injector_density_strength == 6.5F,
                "fluid 3D config should honor run config density injection");
        require(configured.injector_propulsion_strength == 1.6F,
                "fluid 3D config should honor run config injector propulsion");
        require(configured.injector_orbit_radius == 0.24F,
                "fluid 3D config should honor run config injector orbit radius");
        require(configured.injector_orbit_radius_spread == 0.18F,
                "fluid 3D config should honor run config injector orbit radius spread");
        require(configured.injector_orbit_angular_speed == 0.1F,
                "fluid 3D config should honor run config injector orbit speed");
        require(configured.injector_orbit_angular_speed_spread == 1.2F,
                "fluid 3D config should honor run config injector speed spread");
        require(configured.injector_orbit_phase_spread == 0.75F,
                "fluid 3D config should honor run config injector phase spread");
        require(configured.injector_orbit_inclination_degrees == 10.0F,
                "fluid 3D config should honor run config injector inclination");
        require(configured.injector_orbit_inclination_spread_degrees == 50.0F,
                "fluid 3D config should honor run config injector inclination spread");
        require(configured.injector_movement ==
                    cubey::projects::fluid_3d::Fluid3DInjectorMovement::Circle,
                "fluid 3D config should honor run config injector movement");
        require(configured.injector_circle_height == 0.65F,
                "fluid 3D config should honor run config injector circle height");
        require(configured.buoyancy_strength == 1.75F,
                "fluid 3D config should honor run config buoyancy");

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

        bool threw_for_invalid_movement = false;
        try {
            cubey::RunConfig invalid_movement_config;
            invalid_movement_config.injector_movement = "spiral";
            static_cast<void>(cubey::projects::fluid_3d::fluid_3d_config_from_run_config(
                invalid_movement_config));
        } catch (const std::runtime_error&) {
            threw_for_invalid_movement = true;
        }
        require(threw_for_invalid_movement,
                "fluid 3D config should reject unsupported injector movement modes");

        std::vector<cubey::projects::fluid_3d::Fluid3DInjectorState> injectors =
            cubey::projects::fluid_3d::create_fluid_3d_injectors(configured);
        require(injectors.size() == 8, "fluid 3D injector state should match configured count");
        require(injectors.front().radius < injectors.back().radius,
                "fluid 3D injector radii should spread through the volume");
        require(injectors.front().speed < configured.injector_orbit_angular_speed &&
                    injectors.back().speed > configured.injector_orbit_angular_speed,
                "fluid 3D injector speeds should spread around the configured base speed");
        require(injectors.front().inclination_radians < injectors.back().inclination_radians,
                "fluid 3D injector inclinations should spread across the configured band");
        require(injectors[1].phase > injectors[0].phase,
                "fluid 3D injector phases should spread across the configured phase range");
        require(injectors.front().position[1] > 0.64F && injectors.front().position[1] < 0.66F,
                "fluid 3D circle movement should initialize on the configured height plane");
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
        require(gpu_injectors.front().color_density[3] == configured.injector_density_strength,
                "fluid 3D GPU injector should carry configured density injection");
        cubey::projects::fluid_3d::Fluid3DConfig no_propulsion_config = configured;
        no_propulsion_config.injector_propulsion_strength = 0.0F;
        const std::vector<cubey::projects::fluid_3d::Fluid3DInjectorGpu> no_propulsion_gpu =
            cubey::projects::fluid_3d::fluid_3d_injectors_to_gpu(injectors, no_propulsion_config);
        require(length_squared({gpu_injectors.front().velocity_strength[0],
                                gpu_injectors.front().velocity_strength[1],
                                gpu_injectors.front().velocity_strength[2]}) !=
                    length_squared({no_propulsion_gpu.front().velocity_strength[0],
                                    no_propulsion_gpu.front().velocity_strength[1],
                                    no_propulsion_gpu.front().velocity_strength[2]}),
                "fluid 3D GPU injector should add opposite-direction propulsion to carry velocity");

        const cubey::FrameTiming timing{
            .delta_seconds = configured.fixed_delta_seconds,
            .elapsed_seconds = configured.fixed_delta_seconds * 12.0,
            .frame_index = 12,
        };
        gpu_injectors =
            cubey::projects::fluid_3d::update_fluid_3d_injectors(injectors, configured, timing);
        require(injectors.front().position != initial_position,
                "fluid 3D injector update should advance motion position");
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
        require_contains(advect_shader, "layout(rgba16f",
                         "fluid 3D bulk volumes should use explicit RGBA16F storage images");
        require_contains(shadow_shader, "imageSize(shadow_volume)",
                         "fluid 3D shadow dispatch should use the decoupled shadow volume size");
        require_contains(advect_correct_shader, "reversed_density",
                         "fluid 3D should use a MacCormack/BFECC correction pass");
        require_contains(advect_correct_shader, "source_bounds",
                         "fluid 3D MacCormack correction should use a source-neighborhood limiter");
        require_contains(advect_correct_shader, "density_impulse",
                         "fluid 3D correction pass should expose density injection strength");
        require_contains(advect_correct_shader, "buoyancy_force",
                         "fluid 3D correction pass should apply buoyancy");
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
