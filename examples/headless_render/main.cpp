#include <cubey/run_config.h>

#include "headless_render_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey headless_render";
        }
        return cubey::examples::headless_render::run_headless_render(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "headless_render: %s\n", error.what());
        return 1;
    }
}
