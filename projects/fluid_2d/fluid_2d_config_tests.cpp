#include "fluid_2d_config.h"

#include <cubey/frame_clock.h>
#include <cubey/run_config.h>

#include <cstdio>
#include <exception>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        const cubey::projects::fluid_2d::Fluid2DConfig config;
        constexpr std::size_t kExpectedCellCount = std::size_t{256} * std::size_t{144};
        require(config.grid_width == 256, "fluid grid should default to 256 columns");
        require(config.grid_height == 144, "fluid grid should default to 144 rows");
        require(cubey::projects::fluid_2d::field_cell_count(config) == kExpectedCellCount,
                "field cell count should multiply dimensions");
        require(cubey::projects::fluid_2d::field_byte_size(config) ==
                    sizeof(cubey::projects::fluid_2d::FluidCellGpu) * kExpectedCellCount,
                "field byte size should cover one cell per grid location");
        require(config.pressure_iterations == 24,
                "fluid pressure solve should default to 24 Jacobi iterations");
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
                    cubey::projects::fluid_2d::FluidDebugView::Dye,
                "debug view should cycle from pressure to dye");

        cubey::RunConfig run_config;
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
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_2d_config_tests: %s\n", error.what());
        return 1;
    }
}
