#include "fluid_2d_config.h"

#include <cubey/run_config.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    const cubey::projects::fluid_2d::Fluid2DConfig config;
    require(config.grid_width == 256, "fluid grid should default to 256 columns");
    require(config.grid_height == 144, "fluid grid should default to 144 rows");
    require(cubey::projects::fluid_2d::field_cell_count(config) == 256U * 144U,
            "field cell count should multiply dimensions");
    require(cubey::projects::fluid_2d::field_byte_size(config) ==
                sizeof(cubey::projects::fluid_2d::FluidCellGpu) * 256U * 144U,
            "field byte size should cover one cell per grid location");

    cubey::RunConfig run_config;
    require(cubey::projects::fluid_2d::headless_frame_count(run_config) == 120,
            "headless frame count should default to 120 frames");
    run_config.frames = 8;
    require(cubey::projects::fluid_2d::headless_frame_count(run_config) == 8,
            "headless frame count should honor --frames");
}
