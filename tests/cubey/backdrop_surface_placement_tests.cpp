#include <cubey/render/backdrop_surface_placement.h>
#include <cubey/render/ocean_surface_config.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, const char* message) {
    if (std::abs(actual - expected) > 1.0e-4F) {
        throw std::runtime_error(message);
    }
}

void test_requested_height_is_preserved_when_it_clears_the_surface() {
    const cubey::render::BackdropSurfacePlacement placement =
        cubey::render::resolve_backdrop_surface_placement({
            .surface =
                {
                    .nominal_local_height_m = -500.0F,
                    .maximum_local_height_m = -498.0F,
                },
            .foreground =
                {
                    .anchor_world_height_m = 10.0F,
                    .minimum_local_height_m = -1.0F,
                },
            .requested_foreground_height_m = 20.0F,
            .minimum_clearance_m = 0.25F,
        });
    require_near(placement.required_foreground_height_m, 3.25F,
                 "required height should include relief, lower extent, and margin");
    require_near(placement.effective_foreground_height_m, 20.0F,
                 "safe requested height should be preserved");
    require_near(placement.surface_world_translation_y, 490.0F,
                 "translation should place the nominal surface below the anchor");
    require_near(placement.achieved_clearance_m, 17.0F,
                 "result should report the actual lower-bound clearance");
    require(!placement.clearance_adjusted && placement.clearance_satisfied &&
                !placement.intersects_foreground,
            "safe placement should not report an adjustment or intersection");
}

void test_minimum_clearance_raises_an_unsafe_request() {
    const cubey::render::BackdropSurfacePlacement placement =
        cubey::render::resolve_backdrop_surface_placement({
            .surface =
                {
                    .nominal_local_height_m = 0.0F,
                    .maximum_local_height_m = 0.4F,
                },
            .foreground =
                {
                    .anchor_world_height_m = 0.5F,
                    .minimum_local_height_m = -0.5F,
                },
            .requested_foreground_height_m = 0.25F,
            .minimum_clearance_m = 0.1F,
        });
    require_near(placement.required_foreground_height_m, 1.0F,
                 "clearance floor should include the surface excursion");
    require_near(placement.effective_foreground_height_m, 1.0F,
                 "unsafe requested height should be raised");
    require_near(placement.achieved_clearance_m, 0.1F,
                 "enforced placement should meet the requested margin");
    require(placement.clearance_adjusted && placement.clearance_satisfied &&
                !placement.intersects_foreground,
            "enforced placement should report the adjustment");
}

void test_exact_policy_reports_intentional_intersection() {
    const cubey::render::BackdropSurfacePlacement placement =
        cubey::render::resolve_backdrop_surface_placement({
            .surface =
                {
                    .nominal_local_height_m = 0.0F,
                    .maximum_local_height_m = 0.4F,
                },
            .foreground =
                {
                    .anchor_world_height_m = 0.5F,
                    .minimum_local_height_m = -0.5F,
                },
            .requested_foreground_height_m = 0.25F,
            .minimum_clearance_m = 0.1F,
            .clearance_policy = cubey::render::BackdropSurfaceClearancePolicy::ExactRequestedHeight,
        });
    require_near(placement.effective_foreground_height_m, 0.25F,
                 "exact placement should preserve the requested height");
    require_near(placement.achieved_clearance_m, -0.65F,
                 "exact placement should report penetration");
    require(!placement.clearance_adjusted && !placement.clearance_satisfied &&
                placement.intersects_foreground,
            "exact intersecting placement should expose failed clearance");
}

void test_invalid_envelopes_are_rejected() {
    bool rejected = false;
    try {
        static_cast<void>(cubey::render::resolve_backdrop_surface_placement({
            .surface =
                {
                    .nominal_local_height_m = 2.0F,
                    .maximum_local_height_m = 1.0F,
                },
        }));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "surface maximum below nominal should be rejected");
}

void test_ocean_crest_allowance_tracks_enabled_displacement() {
    cubey::render::OceanSurfaceConfig config{};
    require_near(cubey::render::ocean_surface_placement_crest_allowance_m(config), 3.52F,
                 "default ocean crest allowance should include both active cascades");
    config.cascade_enabled[1] = false;
    require_near(cubey::render::ocean_surface_placement_crest_allowance_m(config), 2.08F,
                 "disabled ocean cascades should not contribute to placement");
    config.surface_shape_strength = 0.5F;
    require_near(cubey::render::ocean_surface_placement_crest_allowance_m(config), 1.04F,
                 "ocean crest allowance should track shape strength");
}

} // namespace

int main() {
    try {
        test_requested_height_is_preserved_when_it_clears_the_surface();
        test_minimum_clearance_raises_an_unsafe_request();
        test_exact_policy_reports_intentional_intersection();
        test_invalid_envelopes_are_rejected();
        test_ocean_crest_allowance_tracks_enabled_displacement();
        std::cout << "backdrop_surface_placement_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "backdrop_surface_placement_tests: " << error.what() << '\n';
        return 1;
    }
}
