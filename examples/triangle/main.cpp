#include <cubey/core/run_config.h>

#include "triangle_app.h"

int main(int argc, char** argv) {
    return cubey::run_cli_app(argc, argv,
                              {
                                  .app_name = "triangle",
                                  .default_title = "cubey triangle",
                              },
                              cubey::examples::triangle::run_triangle);
}
