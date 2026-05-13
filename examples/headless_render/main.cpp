#include <cubey/core/run_config.h>

#include "headless_render_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "headless_render",
                                  .default_title = "cubey headless_render",
                              },
                              cubey::examples::headless_render::run_headless_render);
}
