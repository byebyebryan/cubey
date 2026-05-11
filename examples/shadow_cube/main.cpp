#include <cubey/run_config.h>

#include "shadow_cube_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey shadow_cube";
        }
        return cubey::examples::shadow_cube::run_shadow_cube(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "shadow_cube: %s\n", error.what());
        return 1;
    }
}
