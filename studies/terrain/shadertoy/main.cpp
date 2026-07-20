#include "terrain_shadertoy_ref_app.h"
#include "terrain_shadertoy_ref_config.h"

#include <cubey/core/run_config.h>

#include <cstdio>
#include <exception>
#include <vector>

int main(int argc, char** argv) {
    try {
        cubey::projects::terrain_shadertoy_ref::ParsedTerrainShadertoyRefArgs parsed =
            cubey::projects::terrain_shadertoy_ref::parse_terrain_shadertoy_ref_args(argc, argv);
        std::vector<char*> forwarded;
        forwarded.reserve(parsed.forwarded_arguments.size());
        for (std::string& argument : parsed.forwarded_arguments) {
            forwarded.push_back(argument.data());
        }

        cubey::RunConfig run_config =
            cubey::parse_run_config(static_cast<int>(forwarded.size()), forwarded.data());
        if (!run_config.write_config_template_path.empty()) {
            return 0;
        }
        if (run_config.title == "cubey") {
            run_config.title = "cubey terrain ShaderToy reference";
        }
        return cubey::projects::terrain_shadertoy_ref::run_terrain_shadertoy_ref(
            run_config, parsed.reference_config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "terrain_shadertoy_ref: %s\n", error.what());
        return 1;
    }
}
