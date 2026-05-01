#include <cubey/app_config.h>

#include "triangle_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey triangle";
        }
        return cubey::examples::triangle::run_triangle(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "triangle: %s\n", error.what());
        return 1;
    }
}
