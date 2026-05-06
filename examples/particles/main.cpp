#include <cubey/run_config.h>

#include "particles_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey particles";
        }
        return cubey::examples::particles::run_particles(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "particles: %s\n", error.what());
        return 1;
    }
}
