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

        view.zoom_at(0.5F, 320.0F, 180.0F, 640.0F, 360.0F);
        constants = view.push_constants(640, 360);
        require_close(constants.center_x, -0.5F, "center zoom should preserve center x");
        require_close(constants.center_y, 0.0F, "center zoom should preserve center y");
        require_close(constants.scale, 0.675F, "zoom should scale the view");

        view.pan_by_screen_delta(64.0F, -36.0F, 640.0F, 360.0F);
        constants = view.push_constants(640, 360);
        require_close(constants.center_x, -0.74F, "horizontal drag should pan in fractal units");
        require_close(constants.center_y, 0.135F, "vertical drag should follow screen motion");

        view.reset();
        constants = view.push_constants(640, 360);
        require_close(constants.center_x, -0.5F, "reset should restore center x");
        require_close(constants.center_y, 0.0F, "reset should restore center y");
        require_close(constants.scale, 1.35F, "reset should restore scale");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fractal_view_tests: %s\n", error.what());
        return 1;
    }
}
