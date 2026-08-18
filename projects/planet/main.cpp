#include "planet_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::config::ParseResult result;
        cubey::projects::planet::PlanetConfig config =
            cubey::projects::planet::parse_planet_config(argc, argv, &result);
        if (config.common.title == "cubey") {
            config.common.title = "cubey planet";
        }
        if (result.write_config_template_path.has_value()) {
            return 0;
        }
        return cubey::projects::planet::run_planet(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "planet: %s\n", error.what());
        return 1;
    }
}
