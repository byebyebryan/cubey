#include <cubey/core/run_config.h>

#include "window_clear_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "window_clear",
                                  .default_title = "cubey window_clear",
                              },
                              cubey::examples::window_clear::run_window_clear);
}
