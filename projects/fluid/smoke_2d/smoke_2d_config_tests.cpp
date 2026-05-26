#include "smoke_2d_config.h"
#include "smoke_2d_injectors.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
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

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (actual < expected - kTolerance || actual > expected + kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message);
    }
}

void require_not_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) != std::string::npos) {
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

[[nodiscard]] float length_squared(std::array<float, 2> value) {
    return (value[0] * value[0]) + (value[1] * value[1]);
}

} // namespace

int main() {
    try {
        const cubey::projects::fluid::smoke_2d::Smoke2DConfig config;
        constexpr std::size_t kExpectedCellCount = std::size_t{1024} * std::size_t{1024};
        require(config.grid_width == 1024, "smoke grid should default to 1024 columns");
        require(config.grid_height == 1024, "smoke grid should default to 1024 rows");
        require(config.procedural_injector_count == 3,
                "smoke should default to three procedural injectors");
        require(cubey::projects::fluid::smoke_2d::field_cell_count(config) == kExpectedCellCount,
                "field cell count should multiply dimensions");
        require(cubey::projects::fluid::smoke_2d::field_byte_size(config) ==
                    sizeof(cubey::projects::fluid::smoke_2d::SmokeCellGpu) * kExpectedCellCount,
                "field byte size should cover one cell per grid location");
        require(config.pressure_iterations == 32,
                "fluid pressure solve should default to 32 Jacobi iterations");
        require(config.dye_decay_per_second == 0.990F,
                "fluid dye decay should default to controlled linger");
        require(config.velocity_decay_per_second == 0.993F,
                "fluid velocity decay should default to controlled linger");
        require(config.injector_injection_radius == 0.032F,
                "smoke injector radius should be tuned for sharper moving sources");
        require(config.injector_injection_strength == 6.0F,
                "smoke injector strength should default to a visible multi-source impulse");
        require(config.injector_propulsion_strength == 1.0F,
                "smoke injector propulsion should default to neutral scaling");
        require(config.injector_orbit_radius == 0.25F,
                "smoke injector orbit radius should default near the middle of the field");
        require(config.injector_orbit_radius_spread == 0.22F,
                "smoke injector orbit radius spread should default to a broad band");
        require(config.injector_orbit_angular_speed == 0.0F,
                "smoke injector orbit speed should default to a centered signed band");
        require(config.injector_orbit_angular_speed_spread == 0.8F,
                "smoke injector orbit speed spread should default to mixed directions");
        require(config.injector_orbit_phase_spread == 1.0F,
                "smoke injector orbit phase spread should default around a full turn");
        require(config.vorticity_strength == 18.0F,
                "fluid vorticity strength should have a visible default");
        require(!config.obstacles_enabled, "smoke obstacles should default disabled");
        require(cubey::projects::fluid::smoke_2d::scalar_field_byte_size(config) ==
                    sizeof(float) * kExpectedCellCount,
                "scalar field byte size should cover one float per grid location");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Dye) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Velocity,
                "debug view should cycle from dye to velocity");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Velocity) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Divergence,
                "debug view should cycle from velocity to divergence");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Divergence) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Pressure,
                "debug view should cycle from divergence to pressure");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Pressure) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Speed,
                "debug view should cycle from pressure to speed");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Speed) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Vorticity,
                "debug view should cycle from speed to vorticity");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Vorticity) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Obstacle,
                "debug view should cycle from vorticity to obstacle");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Obstacle) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Dye,
                "debug view should cycle from obstacle to dye");

        const cubey::RunConfig default_run_config;
        const cubey::projects::fluid::smoke_2d::Smoke2DConfig default_from_run_config =
            cubey::projects::fluid::smoke_2d::smoke_2d_config_from_run_config(default_run_config);
        require(default_from_run_config.grid_width == config.grid_width,
                "default run config should preserve smoke grid width");
        require(default_from_run_config.grid_height == config.grid_height,
                "default run config should preserve smoke grid height");
        require(default_from_run_config.procedural_injector_count ==
                    config.procedural_injector_count,
                "default run config should preserve smoke injector count");
        require(default_from_run_config.injector_injection_strength ==
                    config.injector_injection_strength,
                "default run config should preserve smoke injector strength");
        require(default_from_run_config.pressure_iterations == config.pressure_iterations,
                "default run config should preserve smoke pressure iterations");
        require(default_from_run_config.dye_decay_per_second == config.dye_decay_per_second,
                "default run config should preserve smoke dye decay");
        require(default_from_run_config.velocity_decay_per_second ==
                    config.velocity_decay_per_second,
                "default run config should preserve smoke velocity decay");
        require(default_from_run_config.injector_injection_radius ==
                    config.injector_injection_radius,
                "default run config should preserve smoke injector radius");
        require(default_from_run_config.injector_orbit_radius == config.injector_orbit_radius,
                "default run config should preserve smoke orbit radius");
        require(default_from_run_config.vorticity_strength == config.vorticity_strength,
                "default run config should preserve smoke vorticity");

        cubey::RunConfig run_config;
        run_config.grid.width = 1024;
        run_config.grid.height = 768;
        run_config.smoke.injectors = 8;
        run_config.smoke.pressure_iterations = 48;
        run_config.smoke.dye_decay = 0.985F;
        run_config.smoke.velocity_decay = 0.991F;
        run_config.smoke.injector_radius = 0.041F;
        run_config.smoke.injector_force = 7.5F;
        run_config.smoke.injector_propulsion = 1.6F;
        run_config.smoke.injector_orbit_radius = 0.24F;
        run_config.smoke.injector_orbit_radius_spread = 0.18F;
        run_config.smoke.injector_orbit_angular_speed = 0.1F;
        run_config.smoke.injector_orbit_angular_speed_spread = 1.2F;
        run_config.smoke.injector_orbit_phase_spread = 0.75F;
        run_config.smoke.vorticity = 24.0F;
        const cubey::projects::fluid::smoke_2d::Smoke2DConfig configured =
            cubey::projects::fluid::smoke_2d::smoke_2d_config_from_run_config(run_config);
        require(configured.grid_width == 1024, "smoke config should honor run config grid width");
        require(configured.grid_height == 768, "smoke config should honor run config grid height");
        require(configured.procedural_injector_count == 8,
                "smoke config should honor run config injector count");
        require(configured.pressure_iterations == 48,
                "smoke config should honor run config pressure iterations");
        require(configured.dye_decay_per_second == 0.985F,
                "smoke config should honor run config dye decay");
        require(configured.velocity_decay_per_second == 0.991F,
                "smoke config should honor run config velocity decay");
        require(configured.injector_injection_radius == 0.041F,
                "smoke config should honor run config injector radius");
        require(configured.injector_injection_strength == 7.5F,
                "smoke config should honor run config injector force");
        require(configured.injector_propulsion_strength == 1.6F,
                "smoke config should honor run config injector propulsion");
        require(configured.injector_orbit_radius == 0.24F,
                "smoke config should honor run config injector orbit radius");
        require(configured.injector_orbit_radius_spread == 0.18F,
                "smoke config should honor run config injector orbit radius spread");
        require(configured.injector_orbit_angular_speed == 0.1F,
                "smoke config should honor run config injector orbit angular speed");
        require(configured.injector_orbit_angular_speed_spread == 1.2F,
                "smoke config should honor run config injector orbit angular speed spread");
        require(configured.injector_orbit_phase_spread == 0.75F,
                "smoke config should honor run config injector orbit phase spread");
        require(configured.vorticity_strength == 24.0F,
                "smoke config should honor run config vorticity");
        require(!configured.obstacles_enabled,
                "smoke config should keep obstacles disabled unless requested");
        run_config.smoke.obstacles = true;
        const cubey::projects::fluid::smoke_2d::Smoke2DConfig configured_with_obstacles =
            cubey::projects::fluid::smoke_2d::smoke_2d_config_from_run_config(run_config);
        require(configured_with_obstacles.obstacles_enabled,
                "smoke config should honor run config obstacle toggle");

        bool threw_for_too_many_injectors = false;
        try {
            cubey::RunConfig invalid_injector_config;
            invalid_injector_config.smoke.injectors =
                cubey::projects::fluid::smoke_2d::kMaxProceduralInjectorCount + 1U;
            static_cast<void>(cubey::projects::fluid::smoke_2d::smoke_2d_config_from_run_config(
                invalid_injector_config));
        } catch (const std::runtime_error&) {
            threw_for_too_many_injectors = true;
        }
        require(threw_for_too_many_injectors,
                "smoke config should reject injector counts above the shader policy limit");
        bool threw_for_invalid_decay = false;
        try {
            cubey::RunConfig invalid_decay_config;
            invalid_decay_config.smoke.dye_decay = 1.2F;
            static_cast<void>(cubey::projects::fluid::smoke_2d::smoke_2d_config_from_run_config(
                invalid_decay_config));
        } catch (const std::runtime_error&) {
            threw_for_invalid_decay = true;
        }
        require(threw_for_invalid_decay, "smoke config should reject dye decay above one");
        bool threw_for_invalid_radius = false;
        try {
            cubey::RunConfig invalid_radius_config;
            invalid_radius_config.smoke.injector_radius = 0.0F;
            static_cast<void>(cubey::projects::fluid::smoke_2d::smoke_2d_config_from_run_config(
                invalid_radius_config));
        } catch (const std::runtime_error&) {
            threw_for_invalid_radius = true;
        }
        require(threw_for_invalid_radius, "smoke config should reject nonpositive injector radius");

        std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorState> injectors =
            cubey::projects::fluid::smoke_2d::create_smoke_2d_injectors(configured);
        require(injectors.size() == 8, "smoke injector state should match configured count");
        require_close(injectors[0].hue, 0.0F, "first injector hue should start at red");
        require_close(injectors[1].hue, 0.125F, "injector hues should spread evenly");
        require(injectors.front().orbit_radius < injectors.back().orbit_radius,
                "smoke injector orbit radii should spread across the configured band");
        require(injectors.front().orbit_radius >=
                    configured.injector_orbit_radius -
                        (configured.injector_orbit_radius_spread * 0.5F) - 0.01F,
                "smoke injector minimum orbit radius should stay inside the configured band");
        require(injectors.back().orbit_radius <=
                    configured.injector_orbit_radius +
                        (configured.injector_orbit_radius_spread * 0.5F) + 0.01F,
                "smoke injector maximum orbit radius should stay inside the configured band");
        require(injectors.front().angular_speed < configured.injector_orbit_angular_speed &&
                    injectors.back().angular_speed > configured.injector_orbit_angular_speed,
                "smoke injector angular speeds should spread around the configured base speed");
        require(injectors[1].anchor_angle > injectors[0].anchor_angle,
                "smoke injector phases should spread across the configured phase range");
        for (std::size_t index = 1; index < injectors.size(); ++index) {
            const float radius_gap =
                injectors[index].orbit_radius - injectors[index - 1].orbit_radius;
            require(radius_gap > 0.010F,
                    "smoke injector radii should avoid clustering inside the orbit band");
            require(radius_gap < 0.040F,
                    "smoke injector radii should stay close to an even orbit-band spread");
        }
        const std::array<float, 2> initial_position = injectors[0].position;
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu> initial_gpu =
            cubey::projects::fluid::smoke_2d::smoke_2d_injectors_to_gpu(injectors, configured);
        require(initial_gpu.size() == 8, "smoke GPU injector state should match state count");
        require(initial_gpu[0].velocity_carry_propulsion[2] > 1.0F,
                "smoke GPU injector should carry source velocity into the fluid");
        require(initial_gpu[0].velocity_carry_propulsion[3] > 0.0F,
                "smoke GPU injector should expose opposite-direction propulsion force");
        cubey::projects::fluid::smoke_2d::Smoke2DConfig no_propulsion_config = configured;
        no_propulsion_config.injector_propulsion_strength = 0.0F;
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu> no_propulsion_gpu =
            cubey::projects::fluid::smoke_2d::smoke_2d_injectors_to_gpu(injectors,
                                                                        no_propulsion_config);
        require(no_propulsion_gpu[0].velocity_carry_propulsion[3] == 0.0F,
                "smoke GPU injector should honor disabled propulsion force");
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu> advanced_gpu =
            cubey::projects::fluid::smoke_2d::update_smoke_2d_injectors(
                injectors, configured,
                {
                    .delta_seconds = configured.fixed_delta_seconds,
                    .elapsed_seconds = configured.fixed_delta_seconds,
                    .frame_index = 1,
                });
        require(advanced_gpu.size() == 8, "smoke injector update should keep configured count");
        require(injectors[0].position != initial_position,
                "smoke injector physics should advance source positions");
        require(cubey::projects::fluid::smoke_2d::smoke_2d_injector_byte_size(configured) ==
                    sizeof(cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu) * 8U,
                "smoke injector byte size should cover one GPU record per injector");
        require(cubey::projects::fluid::smoke_2d::smoke_2d_injector_capacity_byte_size() ==
                    sizeof(cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu) *
                        cubey::projects::fluid::smoke_2d::kMaxProceduralInjectorCount,
                "smoke injector capacity should cover live-editable injector count");

        cubey::projects::fluid::smoke_2d::Smoke2DConfig slow_orbit_config = configured;
        slow_orbit_config.injector_orbit_angular_speed_spread = 0.4F;
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorState> slow_orbits =
            cubey::projects::fluid::smoke_2d::create_smoke_2d_injectors(slow_orbit_config);
        cubey::projects::fluid::smoke_2d::Smoke2DConfig fast_orbit_config = configured;
        fast_orbit_config.injector_orbit_angular_speed_spread = 2.0F;
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorState> fast_orbits =
            cubey::projects::fluid::smoke_2d::create_smoke_2d_injectors(fast_orbit_config);
        require(length_squared(fast_orbits.back().velocity) >
                    length_squared(slow_orbits.back().velocity),
                "injector angular speed spread should increase initial source velocity");

        require(cubey::projects::fluid::smoke_2d::headless_frame_count(run_config) == 120,
                "headless frame count should default to 120 frames");
        run_config.frames = 8;
        require(cubey::projects::fluid::smoke_2d::headless_frame_count(run_config) == 8,
                "headless frame count should honor --frames");

        const cubey::FrameTiming timing =
            cubey::projects::fluid::smoke_2d::fixed_headless_timing(config, 5);
        require(timing.frame_index == 5, "fixed headless timing should preserve frame index");
        require(timing.delta_seconds == config.fixed_delta_seconds,
                "fixed headless timing should use fixed simulation delta");
        require(timing.elapsed_seconds == config.fixed_delta_seconds * 5.0,
                "fixed headless timing should use deterministic elapsed time");

        const std::filesystem::path source_root{CUBEY_SMOKE_2D_SOURCE_DIR};
        const std::string inject_shader =
            read_text_file(source_root / "shaders/smoke_2d_inject.comp");
        require_contains(inject_shader, "cell.velocity.xy += force * source;",
                         "smoke injector force should scale velocity injection");
        require_not_contains(inject_shader, "force * splat * source_active * dt",
                             "smoke injector force should not bypass injection strength");

    } catch (const std::exception& error) {
        std::fprintf(stderr, "smoke_2d_config_tests: %s\n", error.what());
        return 1;
    }
}
