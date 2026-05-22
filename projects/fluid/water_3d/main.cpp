#include "../sim/water_3d/water_3d_app.h"

#include <cubey/core/run_config.h>

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    try {
        const cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        return cubey::projects::fluid::water_3d::run_water_3d(config);
    } catch (const std::exception& error) {
        std::cerr << "water_3d: " << error.what() << '\n';
        return 1;
    }
}
