#include "particle_cubes_app.h"

int main(int argc, char** argv) {
    return cubey::host::run_configured_app(
        argc, argv, {.app_name = "particle_cubes", .default_title = "cubey particle_cubes"},
        cubey::examples::particle_cubes::parse_particle_cubes_config,
        cubey::examples::particle_cubes::run_particle_cubes);
}
