#include <cubey/core/run_config.h>

#include "fluid_2d_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey fluid 2d";
        }
        return cubey::projects::fluid_2d::run_fluid_2d(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_2d: %s\n", error.what());
        return 1;
    }
}
