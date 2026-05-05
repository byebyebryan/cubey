#include <cubey/run_config.h>

#include "fractal_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey fractal";
        }
        return cubey::examples::fractal::run_fractal(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fractal: %s\n", error.what());
        return 1;
    }
}
