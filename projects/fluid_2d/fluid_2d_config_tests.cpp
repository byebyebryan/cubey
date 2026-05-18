#include "fluid_2d_config.h"
#include "fluid_2d_interaction.h"
#include "fluid_2d_injectors.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cstdio>
#include <exception>
#include <stdexcept>
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

} // namespace

int main() {
    try {
        const cubey::projects::fluid_2d::Fluid2DConfig config;
        constexpr std::size_t kExpectedCellCount = std::size_t{1024} * std::size_t{1024};
        require(config.grid_width == 1024, "fluid grid should default to 1024 columns");
        require(config.grid_height == 1024, "fluid grid should default to 1024 rows");
        require(config.procedural_injector_count == 3,
                "fluid should default to three procedural injectors");
        require(cubey::projects::fluid_2d::field_cell_count(config) == kExpectedCellCount,
                "field cell count should multiply dimensions");
        require(cubey::projects::fluid_2d::field_byte_size(config) ==
                    sizeof(cubey::projects::fluid_2d::FluidCellGpu) * kExpectedCellCount,
                "field byte size should cover one cell per grid location");
        require(config.pressure_iterations == 32,
                "fluid pressure solve should default to 32 Jacobi iterations");
        require(config.dye_decay_per_second == 0.990F,
                "fluid dye decay should default to controlled linger");
        require(config.velocity_decay_per_second == 0.993F,
                "fluid velocity decay should default to controlled linger");
        require(config.pointer_injection_radius == 0.035F,
                "fluid pointer injection radius should be tuned for sharper sources");
        require(config.fallback_injection_radius == 0.032F,
                "fluid fallback injection radius should be tuned for sharper moving sources");
        require(config.pointer_injection_strength == 18.0F,
                "fluid pointer injection strength should default to a dramatic impulse");
        require(config.fallback_injection_strength == 6.0F,
                "fluid fallback injection strength should default to a visible multi-source impulse");
        require(config.vorticity_strength == 18.0F,
                "fluid vorticity strength should have a visible default");
        require(!config.obstacles_enabled, "fluid obstacles should default disabled");
        require(cubey::projects::fluid_2d::scalar_field_byte_size(config) ==
                    sizeof(float) * kExpectedCellCount,
                "scalar field byte size should cover one float per grid location");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Dye) ==
                    cubey::projects::fluid_2d::FluidDebugView::Velocity,
                "debug view should cycle from dye to velocity");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Velocity) ==
                    cubey::projects::fluid_2d::FluidDebugView::Divergence,
                "debug view should cycle from velocity to divergence");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Divergence) ==
                    cubey::projects::fluid_2d::FluidDebugView::Pressure,
                "debug view should cycle from divergence to pressure");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Pressure) ==
                    cubey::projects::fluid_2d::FluidDebugView::Speed,
                "debug view should cycle from pressure to speed");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Speed) ==
                    cubey::projects::fluid_2d::FluidDebugView::Vorticity,
                "debug view should cycle from speed to vorticity");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Vorticity) ==
                    cubey::projects::fluid_2d::FluidDebugView::Obstacle,
                "debug view should cycle from vorticity to obstacle");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Obstacle) ==
                    cubey::projects::fluid_2d::FluidDebugView::Dye,
                "debug view should cycle from obstacle to dye");

        cubey::RunConfig run_config;
        run_config.grid_width = 1024;
        run_config.grid_height = 768;
        run_config.injectors = 8;
        const cubey::projects::fluid_2d::Fluid2DConfig configured =
            cubey::projects::fluid_2d::fluid_config_from_run_config(run_config);
        require(configured.grid_width == 1024,
                "fluid config should honor run config grid width");
        require(configured.grid_height == 768,
                "fluid config should honor run config grid height");
        require(configured.procedural_injector_count == 8,
                "fluid config should honor run config injector count");
        require(!configured.obstacles_enabled,
                "fluid config should keep obstacles disabled unless requested");
        run_config.obstacles = true;
        const cubey::projects::fluid_2d::Fluid2DConfig configured_with_obstacles =
            cubey::projects::fluid_2d::fluid_config_from_run_config(run_config);
        require(configured_with_obstacles.obstacles_enabled,
                "fluid config should honor run config obstacle toggle");

        bool threw_for_too_many_injectors = false;
        try {
            cubey::RunConfig invalid_injector_config;
            invalid_injector_config.injectors =
                cubey::projects::fluid_2d::kMaxProceduralInjectorCount + 1U;
            static_cast<void>(
                cubey::projects::fluid_2d::fluid_config_from_run_config(invalid_injector_config));
        } catch (const std::runtime_error&) {
            threw_for_too_many_injectors = true;
        }
        require(threw_for_too_many_injectors,
                "fluid config should reject injector counts above the shader policy limit");

        std::vector<cubey::projects::fluid_2d::Fluid2DInjectorState> injectors =
            cubey::projects::fluid_2d::create_fluid_2d_injectors(configured);
        require(injectors.size() == 8, "fluid injector state should match configured count");
        require_close(injectors[0].hue, 0.0F, "first injector hue should start at red");
        require_close(injectors[1].hue, 0.125F, "injector hues should spread evenly");
        const std::array<float, 2> initial_position = injectors[0].position;
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorGpu> initial_gpu =
            cubey::projects::fluid_2d::fluid_2d_injectors_to_gpu(injectors, configured);
        require(initial_gpu.size() == 8, "fluid GPU injector state should match state count");
        require(initial_gpu[0].velocity_carry_propulsion[2] > 1.0F,
                "fluid GPU injector should carry source velocity into the fluid");
        require(initial_gpu[0].velocity_carry_propulsion[3] > 0.0F,
                "fluid GPU injector should expose opposite-direction propulsion force");
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorGpu> advanced_gpu =
            cubey::projects::fluid_2d::update_fluid_2d_injectors(
                injectors, configured,
                {
                    .delta_seconds = configured.fixed_delta_seconds,
                    .elapsed_seconds = configured.fixed_delta_seconds,
                    .frame_index = 1,
                });
        require(advanced_gpu.size() == 8, "fluid injector update should keep configured count");
        require(injectors[0].position != initial_position,
                "fluid injector physics should advance source positions");
        require(cubey::projects::fluid_2d::fluid_2d_injector_byte_size(configured) ==
                    sizeof(cubey::projects::fluid_2d::Fluid2DInjectorGpu) * 8U,
                "fluid injector byte size should cover one GPU record per injector");

        require(cubey::projects::fluid_2d::headless_frame_count(run_config) == 120,
                "headless frame count should default to 120 frames");
        run_config.frames = 8;
        require(cubey::projects::fluid_2d::headless_frame_count(run_config) == 8,
                "headless frame count should honor --frames");

        const cubey::FrameTiming timing =
            cubey::projects::fluid_2d::fixed_headless_timing(config, 5);
        require(timing.frame_index == 5, "fixed headless timing should preserve frame index");
        require(timing.delta_seconds == config.fixed_delta_seconds,
                "fixed headless timing should use fixed simulation delta");
        require(timing.elapsed_seconds == config.fixed_delta_seconds * 5.0,
                "fixed headless timing should use deterministic elapsed time");

        const cubey::projects::fluid_2d::FrameInjection injection =
            cubey::projects::fluid_2d::frame_injection_from_pointer(
                {.x = 25.0, .y = 10.0}, {.x = 5.0, .y = -2.0}, {.width = 100, .height = 50});
        require(injection.active, "pointer injection should become active for a nonzero window");
        require_close(injection.xy[0], 0.25F,
                      "pointer injection x should normalize in window coordinates");
        require_close(injection.xy[1], 0.2F,
                      "pointer injection y should follow GLFW top-left window coordinates");
        require_close(injection.force[0], 4.5F,
                      "pointer force x should normalize from drag delta");
        require_close(injection.force[1], -3.6F,
                      "pointer force y should preserve GLFW drag direction");

        const cubey::projects::fluid_2d::FrameInjection inactive =
            cubey::projects::fluid_2d::frame_injection_from_pointer(
                {.x = 25.0, .y = 10.0}, {.x = 5.0, .y = -2.0}, {.width = 0, .height = 50});
        require(!inactive.active, "pointer injection should stay inactive for zero-size windows");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_2d_config_tests: %s\n", error.what());
        return 1;
    }
}
