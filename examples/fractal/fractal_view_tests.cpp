#include "fractal_view.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

void require_close(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) > 0.0001F) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        cubey::examples::fractal::FractalView view;
        cubey::examples::fractal::FractalPushConstants constants = view.push_constants(640, 360);
        require_close(constants.center_x, -0.5F, "initial center x should match Mandelbrot view");
        require_close(constants.center_y, 0.0F, "initial center y should match Mandelbrot view");
        require_close(constants.scale, 1.35F, "initial scale should match Mandelbrot view");
        require_close(constants.aspect, 640.0F / 360.0F, "aspect should come from extent");

        view.set_view(-0.5F, 0.0F, 0.675F);
        constants = view.push_constants(640, 360);
        require_close(constants.center_x, -0.5F, "set view should update center x");
        require_close(constants.center_y, 0.0F, "set view should update center y");
        require_close(constants.scale, 0.675F, "set view should update scale");

        view.set_view(-0.74F, 0.135F, 0.675F);
        constants = view.push_constants(640, 360);
        require_close(constants.center_x, -0.74F, "set view should accept panned center x");
        require_close(constants.center_y, 0.135F, "set view should accept panned center y");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fractal_view_tests: %s\n", error.what());
        return 1;
    }
}
