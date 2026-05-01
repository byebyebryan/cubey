#include <cubey/app_config.h>

#include "spinning_cube_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey spinning_cube";
        }
        return cubey::examples::spinning_cube::run_spinning_cube(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "spinning_cube: %s\n", error.what());
        return 1;
    }
}
