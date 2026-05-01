#include <cubey/app_config.h>

#include "window_clear_app.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        cubey::RunConfig config = cubey::parse_run_config(argc, argv);
        if (config.title == "cubey") {
            config.title = "cubey window_clear";
        }
        return cubey::examples::window_clear::run_window_clear(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "window_clear: %s\n", error.what());
        return 1;
    }
}
