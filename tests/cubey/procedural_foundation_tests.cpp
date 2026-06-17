#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/noise.h>
#include <cubey/procedural/operators.h>

#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

template <typename Fn> void require_throws(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

void test_procedural_scalar_field_indexes_centered_samples() {
    const cubey::procedural::Grid2DDesc desc{
        .width = 5,
        .height = 3,
        .cell_size = 2.0F,
        .origin_x = 10.0F,
        .origin_y = -4.0F,
    };
    cubey::procedural::ScalarField2D field(desc, 1.0F);

    require(field.sample_count() == 15U, "scalar field should allocate width * height samples");
    require(field.index(2U, 1U) == 7U, "scalar field should use row-major indexing");
    require_near(cubey::procedural::grid_sample_x(desc, 0U), 6.0F, 0.0001F,
                 "grid x samples should be centered around origin");
    require_near(cubey::procedural::grid_sample_x(desc, 4U), 14.0F, 0.0001F,
                 "grid x samples should be centered around origin");
    require_near(cubey::procedural::grid_sample_y(desc, 0U), -6.0F, 0.0001F,
                 "grid y samples should be centered around origin");
    require_near(cubey::procedural::grid_sample_y(desc, 2U), -2.0F, 0.0001F,
                 "grid y samples should be centered around origin");

    field.at(2U, 1U) = 5.0F;
    require_near(field.at(2U, 1U), 5.0F, 0.0001F,
                 "scalar field should expose writable indexed samples");
    require_throws([&field] { (void)field.at(5U, 0U); },
                   "scalar field should reject out-of-range samples");
}

void test_procedural_scalar_field_summarizes_and_normalizes() {
    cubey::procedural::ScalarField2D field({.width = 2, .height = 2}, 0.0F);
    field.at(0U, 0U) = -2.0F;
    field.at(1U, 0U) = 0.0F;
    field.at(0U, 1U) = 2.0F;
    field.at(1U, 1U) = 4.0F;

    const cubey::procedural::ScalarFieldStats stats = field.summarize();
    require_near(stats.min, -2.0F, 0.0001F, "field summary should track minimum");
    require_near(stats.max, 4.0F, 0.0001F, "field summary should track maximum");
    require_near(stats.span, 6.0F, 0.0001F, "field summary should track span");
    require_near(stats.mean, 1.0F, 0.0001F, "field summary should track mean");

    cubey::procedural::normalize_to_unit(field);
    require_near(field.at(0U, 0U), 0.0F, 0.0001F, "normalization should map the minimum to zero");
    require_near(field.at(1U, 1U), 1.0F, 0.0001F, "normalization should map the maximum to one");
}

void test_procedural_box_blur_preserves_dimensions_and_smooths_impulse() {
    cubey::procedural::ScalarField2D field({.width = 3, .height = 3}, 0.0F);
    field.at(1U, 1U) = 16.0F;

    const cubey::procedural::ScalarField2D blurred = cubey::procedural::box_blur_3x3(field);
    require(blurred.desc().width == 3U && blurred.desc().height == 3U,
            "box blur should preserve field dimensions");
    require_near(blurred.at(1U, 1U), 4.0F, 0.0001F,
                 "box blur should apply the weighted 3x3 kernel at the center");
    require_near(blurred.at(0U, 0U), 16.0F / 9.0F, 0.0001F,
                 "box blur should renormalize weights at edges");
}

void test_procedural_noise_is_deterministic_and_bounded() {
    const float first = cubey::procedural::value_noise_2d(1.25F, -3.75F, 17U);
    const float second = cubey::procedural::value_noise_2d(1.25F, -3.75F, 17U);
    require_near(first, second, 0.000001F, "value noise should be deterministic");
    require(first >= -1.0F && first <= 1.0F, "value noise should remain in signed unit range");

    const float changed_seed = cubey::procedural::value_noise_2d(1.25F, -3.75F, 18U);
    require(std::fabs(first - changed_seed) > 0.000001F,
            "value noise should vary when the seed changes");

    const float fbm = cubey::procedural::fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5});
    require(fbm >= -1.0F && fbm <= 1.0F, "fbm should remain in signed unit range");

    const float ridged = cubey::procedural::ridged_fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5});
    require(ridged >= 0.0F && ridged <= 1.0F, "ridged fbm should remain in unit range");
}
