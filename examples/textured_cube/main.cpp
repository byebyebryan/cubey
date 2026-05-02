#include <cubey/app_config.h>

#include "textured_cube_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey textured_cube";
        }
        return cubey::examples::textured_cube::run_textured_cube(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "textured_cube: %s\n", error.what());
        return 1;
    }
}
