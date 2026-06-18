#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/noise.h>
#include <cubey/procedural/operators.h>
#include <cubey/procedural/source_fields.h>

#include "source_file_test_helpers.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
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

void require_finite_unit(float value, const char* message) {
    require(std::isfinite(value), message);
    require(value >= -1.0001F && value <= 1.0001F, message);
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

void test_procedural_field_composition_transforms_values() {
    cubey::procedural::ScalarField2D field({.width = 2, .height = 2}, 0.0F);
    field.at(0U, 0U) = -2.0F;
    field.at(1U, 0U) = -0.5F;
    field.at(0U, 1U) = 0.5F;
    field.at(1U, 1U) = 2.0F;

    const cubey::procedural::ScalarField2D clamped =
        cubey::procedural::clamp_field(field, -1.0F, 1.0F);
    require_near(clamped.at(0U, 0U), -1.0F, 0.0001F, "clamp should clamp low values");
    require_near(clamped.at(1U, 1U), 1.0F, 0.0001F, "clamp should clamp high values");

    const cubey::procedural::ScalarField2D remapped =
        cubey::procedural::remap_field(field, -1.0F, 1.0F, 10.0F, 20.0F);
    require_near(remapped.at(0U, 0U), 10.0F, 0.0001F, "remap should clamp below input range");
    require_near(remapped.at(1U, 0U), 12.5F, 0.0001F, "remap should scale in-range values");
    require_near(remapped.at(1U, 1U), 20.0F, 0.0001F, "remap should clamp above input range");

    const cubey::procedural::ScalarField2D stepped =
        cubey::procedural::smoothstep_field(field, -1.0F, 1.0F);
    require_near(stepped.at(1U, 0U), 0.15625F, 0.0001F,
                 "smoothstep field should apply scalar smoothstep");

    const cubey::procedural::ScalarField2D inverted = cubey::procedural::invert_unit_field(stepped);
    require_near(inverted.at(1U, 0U), 0.84375F, 0.0001F,
                 "unit inversion should saturate and invert field samples");

    const cubey::procedural::ScalarField2D ridged =
        cubey::procedural::ridge_profile_field(field, 2.0F);
    require_near(ridged.at(0U, 0U), 0.0F, 0.0001F,
                 "ridge profile should clamp values outside the ridge");
    require_near(ridged.at(0U, 1U), 0.25F, 0.0001F, "ridge profile should shape values near zero");
}

void test_procedural_field_shaping_converts_and_terraces_unit_values() {
    require_near(cubey::procedural::signed_to_unit(-1.0F), 0.0F, 0.0001F,
                 "signed-to-unit should map negative one to zero");
    require_near(cubey::procedural::signed_to_unit(2.0F), 1.0F, 0.0001F,
                 "signed-to-unit should saturate high values");
    require_near(cubey::procedural::unit_to_signed(0.25F), -0.5F, 0.0001F,
                 "unit-to-signed should remap unit values");
    require_near(cubey::procedural::pow_unit(0.25F, 0.5F), 0.5F, 0.0001F,
                 "pow-unit should apply exponent after saturation");
    require_near(cubey::procedural::terrace_unit(0.32F, 4U, 0.0F), 0.0F, 0.0001F,
                 "hard terrace should hold the lower step");
    require_near(cubey::procedural::terrace_unit(0.34F, 4U, 0.0F), 1.0F / 3.0F, 0.0001F,
                 "hard terrace should advance at interval boundaries");
    require_near(cubey::procedural::terrace_unit(0.30F, 4U, 0.25F), 0.216F, 0.001F,
                 "soft terrace should blend near the upper interval edge");

    cubey::procedural::ScalarField2D field({.width = 2, .height = 2}, 0.0F);
    field.at(0U, 0U) = -1.0F;
    field.at(1U, 0U) = 0.0F;
    field.at(0U, 1U) = 0.25F;
    field.at(1U, 1U) = 1.0F;

    const cubey::procedural::ScalarField2D unit = cubey::procedural::signed_to_unit_field(field);
    require_near(unit.at(0U, 0U), 0.0F, 0.0001F,
                 "signed-to-unit field should convert negative samples");
    require_near(unit.at(1U, 0U), 0.5F, 0.0001F,
                 "signed-to-unit field should convert zero samples");

    const cubey::procedural::ScalarField2D signed_field =
        cubey::procedural::unit_to_signed_field(unit);
    require_near(signed_field.at(1U, 0U), 0.0F, 0.0001F,
                 "unit-to-signed field should invert signed-to-unit around zero");

    const cubey::procedural::ScalarField2D powered = cubey::procedural::pow_unit_field(unit, 2.0F);
    require_near(powered.at(1U, 0U), 0.25F, 0.0001F, "pow-unit field should shape each sample");

    const cubey::procedural::ScalarField2D terraced =
        cubey::procedural::terrace_unit_field(unit, 4U, 0.0F);
    require_near(terraced.at(1U, 0U), 1.0F / 3.0F, 0.0001F,
                 "terrace field should quantize each sample");
}

void test_procedural_field_composition_combines_matching_fields() {
    cubey::procedural::ScalarField2D lhs({.width = 2, .height = 2}, 0.0F);
    cubey::procedural::ScalarField2D rhs(lhs.desc(), 0.0F);
    cubey::procedural::ScalarField2D mask(lhs.desc(), 0.0F);
    lhs.at(0U, 0U) = 1.0F;
    lhs.at(1U, 0U) = 2.0F;
    lhs.at(0U, 1U) = 3.0F;
    lhs.at(1U, 1U) = 4.0F;
    rhs.at(0U, 0U) = 8.0F;
    rhs.at(1U, 0U) = 6.0F;
    rhs.at(0U, 1U) = 4.0F;
    rhs.at(1U, 1U) = 2.0F;
    mask.at(0U, 0U) = 0.0F;
    mask.at(1U, 0U) = 0.25F;
    mask.at(0U, 1U) = 0.75F;
    mask.at(1U, 1U) = 1.0F;

    require_near(cubey::procedural::add_fields(lhs, rhs).at(0U, 0U), 9.0F, 0.0001F,
                 "add field should add samples");
    require_near(cubey::procedural::subtract_fields(rhs, lhs).at(1U, 0U), 4.0F, 0.0001F,
                 "subtract field should subtract samples");
    require_near(cubey::procedural::multiply_fields(lhs, rhs).at(1U, 1U), 8.0F, 0.0001F,
                 "multiply field should multiply samples");
    require_near(cubey::procedural::min_fields(lhs, rhs).at(0U, 1U), 3.0F, 0.0001F,
                 "min field should select smaller samples");
    require_near(cubey::procedural::max_fields(lhs, rhs).at(0U, 1U), 4.0F, 0.0001F,
                 "max field should select larger samples");

    const cubey::procedural::ScalarField2D blended =
        cubey::procedural::blend_fields(lhs, rhs, mask);
    require(blended.desc().width == lhs.desc().width && blended.desc().height == lhs.desc().height,
            "blend should preserve field dimensions");
    require_near(blended.at(0U, 0U), 1.0F, 0.0001F, "blend mask zero should keep lhs");
    require_near(blended.at(1U, 0U), 3.0F, 0.0001F, "blend mask should interpolate between fields");
    require_near(blended.at(1U, 1U), 2.0F, 0.0001F, "blend mask one should keep rhs");
}

void test_procedural_field_composition_rejects_invalid_inputs() {
    cubey::procedural::ScalarField2D field({.width = 2, .height = 2}, 0.0F);
    cubey::procedural::ScalarField2D other_size({.width = 3, .height = 2}, 0.0F);
    cubey::procedural::ScalarField2D other_origin({.width = 2, .height = 2, .origin_x = 1.0F},
                                                  0.0F);

    require_throws([&] { (void)cubey::procedural::add_fields(field, other_size); },
                   "binary field operators should reject mismatched dimensions");
    require_throws([&] { (void)cubey::procedural::blend_fields(field, other_origin, field); },
                   "blend should reject mismatched descriptors");
    require_throws([&] { (void)cubey::procedural::remap_field(field, 1.0F, 1.0F, 0.0F, 1.0F); },
                   "remap should reject zero input range");
    require_throws([&] { (void)cubey::procedural::pow_unit(0.5F, 0.0F); },
                   "pow-unit should reject zero exponent");
    require_throws([&] { (void)cubey::procedural::pow_unit_field(field, -1.0F); },
                   "pow-unit field should reject negative exponents");
    require_throws([&] { (void)cubey::procedural::terrace_unit(0.5F, 1U, 0.0F); },
                   "terrace should reject a single step");
}

void test_procedural_slope_curvature_handles_flat_ramp_and_peak() {
    const cubey::procedural::ScalarField2D flat({.width = 3, .height = 3, .cell_size = 2.0F}, 7.0F);
    const cubey::procedural::SlopeCurvature2D flat_analysis =
        cubey::procedural::compute_slope_curvature(flat);
    require_near(flat_analysis.max_slope, 0.0F, 0.0001F, "flat field should have zero slope");
    require_near(flat_analysis.max_abs_curvature, 0.0F, 0.0001F,
                 "flat field should have zero curvature");

    cubey::procedural::ScalarField2D ramp({.width = 3, .height = 3, .cell_size = 2.0F}, 0.0F);
    for (std::uint32_t y = 0; y < ramp.desc().height; ++y) {
        for (std::uint32_t x = 0; x < ramp.desc().width; ++x) {
            ramp.at(x, y) = static_cast<float>(x) * 4.0F;
        }
    }
    const cubey::procedural::SlopeCurvature2D ramp_analysis =
        cubey::procedural::compute_slope_curvature(ramp);
    require_near(ramp_analysis.slope.at(1U, 1U), 2.0F, 0.0001F,
                 "linear ramp slope should scale by cell size");
    require_near(ramp_analysis.curvature.at(1U, 1U), 0.0F, 0.0001F,
                 "linear ramp center should have zero curvature");

    cubey::procedural::ScalarField2D peak({.width = 3, .height = 3, .cell_size = 1.0F}, 0.0F);
    peak.at(1U, 1U) = 9.0F;
    const cubey::procedural::SlopeCurvature2D peak_analysis =
        cubey::procedural::compute_slope_curvature(peak);
    require_near(peak_analysis.slope.at(1U, 1U), 0.0F, 0.0001F,
                 "symmetric peak center should have zero centered slope");
    require_near(peak_analysis.curvature.at(1U, 1U), -9.0F, 0.0001F,
                 "peak center should be negative convex curvature");
    require_near(peak_analysis.max_abs_curvature, 9.0F, 0.0001F,
                 "peak curvature should drive max absolute curvature");
}

void test_procedural_local_relief_tracks_neighborhood_windows() {
    cubey::procedural::ScalarField2D field({.width = 3, .height = 3, .cell_size = 1.0F}, 0.0F);
    float value = 1.0F;
    for (std::uint32_t y = 0; y < field.desc().height; ++y) {
        for (std::uint32_t x = 0; x < field.desc().width; ++x) {
            field.at(x, y) = value;
            value += 1.0F;
        }
    }

    const cubey::procedural::LocalRelief2D relief =
        cubey::procedural::compute_local_relief(field, 1U);
    require_near(relief.local_min.at(1U, 1U), 1.0F, 0.0001F,
                 "center relief window should see the field minimum");
    require_near(relief.local_max.at(1U, 1U), 9.0F, 0.0001F,
                 "center relief window should see the field maximum");
    require_near(relief.local_mean.at(1U, 1U), 5.0F, 0.0001F,
                 "center relief window should average the full 3x3 region");
    require_near(relief.local_span.at(1U, 1U), 8.0F, 0.0001F,
                 "center relief window should expose local span");
    require_near(relief.local_min.at(0U, 0U), 1.0F, 0.0001F,
                 "corner relief window should clamp to valid samples");
    require_near(relief.local_max.at(0U, 0U), 5.0F, 0.0001F,
                 "corner relief window should clamp to valid samples");
    require_near(relief.local_mean.at(0U, 0U), 3.0F, 0.0001F,
                 "corner relief window should average only valid samples");
    require_near(relief.local_span.at(0U, 0U), 4.0F, 0.0001F,
                 "corner relief window should expose clamped local span");

    const cubey::procedural::LocalRelief2D radius_zero =
        cubey::procedural::compute_local_relief(field, 0U);
    require_near(radius_zero.local_mean.at(2U, 2U), 9.0F, 0.0001F,
                 "zero-radius relief should preserve the source sample");
    require_near(radius_zero.local_span.at(2U, 2U), 0.0F, 0.0001F,
                 "zero-radius relief should have zero span");
}

void test_procedural_operators_include_smootherstep() {
    require_near(cubey::procedural::smootherstep01(-1.0F), 0.0F, 0.0001F,
                 "smootherstep should saturate below zero");
    require_near(cubey::procedural::smootherstep01(0.5F), 0.5F, 0.0001F,
                 "smootherstep midpoint should stay centered");
    require_near(cubey::procedural::smootherstep01(2.0F), 1.0F, 0.0001F,
                 "smootherstep should saturate above one");
}

void test_procedural_shader_random_helpers_are_shared() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string random_source =
        cubey::tests::read_source_file(root / "shaders/cubey/procedural/random.glsl");
    const std::string noise_source =
        cubey::tests::read_source_file(root / "shaders/cubey/procedural/noise.glsl");

    cubey::tests::require_contains(random_source, "cubey_proc_hash01_u32",
                                   "shader random helpers should expose uint hash-to-unit");
    cubey::tests::require_contains(random_source, "cubey_proc_hash_pcg_2d",
                                   "shader random helpers should expose shared 2D PCG hash");
    cubey::tests::require_contains(random_source, "cubey_proc_hash_pcg_3d",
                                   "shader random helpers should expose shared 3D PCG hash");
    cubey::tests::require_contains(noise_source, "#include \"cubey/procedural/random.glsl\"",
                                   "shader noise helpers should consume shared random helpers");
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

void test_procedural_legacy_noise_golden_values_are_stable() {
    require_near(cubey::procedural::value_noise_2d(1.25F, -3.75F, 17U), -0.457798481F, 0.000001F,
                 "legacy value noise should keep stable samples");
    require_near(cubey::procedural::fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5}), -0.063320771F,
                 0.000001F, "legacy fbm should keep stable samples");
    require_near(cubey::procedural::ridged_fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5}), 0.877368033F,
                 0.000001F, "legacy ridged fbm should keep stable samples");
}

void test_procedural_3d_noise_is_deterministic_and_stable() {
    const float first = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U);
    const float second = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U);
    require_near(first, second, 0.000001F, "3D value noise should be deterministic");
    require(first >= -1.0F && first <= 1.0F, "3D value noise should remain in signed unit range");

    const float changed_seed = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 18U);
    require(std::fabs(first - changed_seed) > 0.000001F,
            "3D value noise should vary when the seed changes");

    const float fbm = cubey::procedural::fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5});
    require(fbm >= -1.0F && fbm <= 1.0F, "3D fbm should remain in signed unit range");

    const float ridged = cubey::procedural::ridged_fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5});
    require(ridged >= 0.0F && ridged <= 1.0F, "3D ridged fbm should remain in unit range");

    require_near(cubey::procedural::hash_to_unit(123456789U), 0.659940481F, 0.000001F,
                 "3D-compatible hash-to-unit should keep stable samples");
    require(cubey::procedural::hash_u32(-2, 7, 4, 19U) == 2469915974U,
            "3D-compatible hash should keep stable samples");
    require_near(cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U), 0.302227825F,
                 0.000001F, "3D value noise should keep stable samples");
    require_near(cubey::procedural::fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5}), -0.123893112F,
                 0.000001F, "3D fbm should keep stable samples");
    require_near(cubey::procedural::ridged_fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5}),
                 0.767563224F, 0.000001F, "3D ridged fbm should keep stable samples");
}

void test_procedural_coherent_noise_wraps_fastnoise_lite() {
    using cubey::procedural::CoherentCellularDistance;
    using cubey::procedural::CoherentCellularReturn;
    using cubey::procedural::CoherentDomainWarpConfig;
    using cubey::procedural::CoherentDomainWarpFractalType;
    using cubey::procedural::CoherentDomainWarpType;
    using cubey::procedural::CoherentFractalType;
    using cubey::procedural::CoherentNoiseConfig;
    using cubey::procedural::CoherentNoiseType;

    const CoherentNoiseConfig base{
        .seed = 47,
        .frequency = 0.035F,
        .noise_type = CoherentNoiseType::OpenSimplex2,
        .fractal_type = CoherentFractalType::Fbm,
        .octaves = 4,
        .lacunarity = 2.08F,
        .gain = 0.52F,
        .weighted_strength = 0.18F,
    };
    const float first = cubey::procedural::sample_coherent_noise_2d(12.5F, -7.25F, base);
    const float second = cubey::procedural::sample_coherent_noise_2d(12.5F, -7.25F, base);
    require_near(first, second, 0.000001F, "coherent noise should be deterministic");
    require_finite_unit(first, "coherent noise should stay in signed unit range");

    CoherentNoiseConfig changed_seed = base;
    changed_seed.seed = 48;
    const float changed = cubey::procedural::sample_coherent_noise_2d(12.5F, -7.25F, changed_seed);
    require(std::fabs(first - changed) > 0.000001F,
            "coherent noise should change when the seed changes");

    constexpr std::array noise_types{
        CoherentNoiseType::OpenSimplex2S,
        CoherentNoiseType::Perlin,
        CoherentNoiseType::ValueCubic,
        CoherentNoiseType::Value,
    };
    for (const CoherentNoiseType noise_type : noise_types) {
        CoherentNoiseConfig config = base;
        config.noise_type = noise_type;
        config.fractal_type = CoherentFractalType::Ridged;
        require_finite_unit(cubey::procedural::sample_coherent_noise_3d(2.0F, -3.0F, 4.0F, config),
                            "coherent 3D noise modes should be finite and bounded");
    }

    CoherentNoiseConfig cellular = base;
    cellular.noise_type = CoherentNoiseType::Cellular;
    cellular.fractal_type = CoherentFractalType::None;
    cellular.cellular_distance = CoherentCellularDistance::Hybrid;
    cellular.cellular_return = CoherentCellularReturn::Distance2Add;
    require_finite_unit(cubey::procedural::sample_coherent_noise_2d(-4.0F, 8.0F, cellular),
                        "coherent cellular noise should be finite and bounded");

    CoherentNoiseConfig ping_pong = base;
    ping_pong.fractal_type = CoherentFractalType::PingPong;
    ping_pong.ping_pong_strength = 1.75F;
    require_finite_unit(cubey::procedural::sample_coherent_noise_2d(9.0F, 3.0F, ping_pong),
                        "coherent ping-pong fractal noise should be finite and bounded");

    const CoherentDomainWarpConfig warp{
        .seed = 91,
        .frequency = 0.08F,
        .warp_type = CoherentDomainWarpType::BasicGrid,
        .fractal_type = CoherentDomainWarpFractalType::Progressive,
        .octaves = 3,
        .amplitude = 0.50F,
    };
    const cubey::procedural::CoherentWarp2D warp_a =
        cubey::procedural::domain_warp_2d(1.50F, -2.25F, warp);
    const cubey::procedural::CoherentWarp2D warp_b =
        cubey::procedural::domain_warp_2d(1.50F, -2.25F, warp);
    require_near(warp_a.x, warp_b.x, 0.000001F, "2D domain warp should be deterministic");
    require_near(warp_a.y, warp_b.y, 0.000001F, "2D domain warp should be deterministic");
    require(std::fabs(warp_a.x - 1.50F) > 0.000001F || std::fabs(warp_a.y + 2.25F) > 0.000001F,
            "2D domain warp should move at least one coordinate");

    CoherentDomainWarpConfig warp3 = warp;
    warp3.warp_type = CoherentDomainWarpType::OpenSimplex2Reduced;
    warp3.fractal_type = CoherentDomainWarpFractalType::Independent;
    const cubey::procedural::CoherentWarp3D warped =
        cubey::procedural::domain_warp_3d(1.0F, 2.0F, 3.0F, warp3);
    require(std::isfinite(warped.x) && std::isfinite(warped.y) && std::isfinite(warped.z),
            "3D domain warp should produce finite coordinates");
}

void test_procedural_source_fields_wrap_legacy_noise_backends() {
    cubey::procedural::NoiseSource2D config{
        .backend = cubey::procedural::NoiseSource2DBackend::LegacyFbm,
        .output = cubey::procedural::NoiseSource2DOutput::Signed,
        .seed = 42U,
        .legacy_fbm = {.octaves = 5},
        .domain = {.x_scale = 1.5F, .y_scale = 0.75F, .x_offset = 2.0F, .y_offset = -3.0F},
    };

    const float expected_signed = cubey::procedural::fbm_2d(
        (1.25F * 1.5F) + 2.0F, (-0.5F * 0.75F) - 3.0F, 42U, {.octaves = 5});
    require_near(cubey::procedural::sample_noise_source_2d(1.25F, -0.5F, config), expected_signed,
                 0.000001F, "legacy source should apply domain transform before sampling FBM");

    config.output = cubey::procedural::NoiseSource2DOutput::Unit;
    require_near(cubey::procedural::sample_noise_source_2d(1.25F, -0.5F, config),
                 (expected_signed * 0.5F) + 0.5F, 0.000001F,
                 "legacy source should map signed FBM to unit output");

    config.backend = cubey::procedural::NoiseSource2DBackend::LegacyRidgedFbm;
    config.output = cubey::procedural::NoiseSource2DOutput::Unit;
    const float expected_ridged = cubey::procedural::ridged_fbm_2d(
        (1.25F * 1.5F) + 2.0F, (-0.5F * 0.75F) - 3.0F, 42U, {.octaves = 5});
    require_near(cubey::procedural::sample_noise_source_2d(1.25F, -0.5F, config), expected_ridged,
                 0.000001F, "ridged legacy source should remain naturally unit range");
}

void test_procedural_source_fields_wrap_coherent_noise_backend() {
    const cubey::procedural::NoiseSource2D source{
        .backend = cubey::procedural::NoiseSource2DBackend::CoherentNoise,
        .output = cubey::procedural::NoiseSource2DOutput::Unit,
        .seed = 47U,
        .coherent =
            {
                .frequency = 0.035F,
                .noise_type = cubey::procedural::CoherentNoiseType::OpenSimplex2,
                .fractal_type = cubey::procedural::CoherentFractalType::Fbm,
                .octaves = 4,
                .lacunarity = 2.08F,
                .gain = 0.52F,
                .weighted_strength = 0.18F,
            },
        .domain = {.x_scale = 0.8F, .y_scale = 1.2F, .x_offset = 5.0F, .y_offset = -2.0F},
    };
    cubey::procedural::CoherentNoiseConfig coherent = source.coherent;
    coherent.seed = 47;
    const float expected = cubey::procedural::sample_coherent_noise_2d(
        (12.5F * 0.8F) + 5.0F, (-7.25F * 1.2F) - 2.0F, coherent);
    require_near(cubey::procedural::sample_noise_source_2d(12.5F, -7.25F, source),
                 (expected * 0.5F) + 0.5F, 0.000001F,
                 "coherent source should use shared seed and domain transform");
}

void test_procedural_source_fields_apply_optional_domain_warp() {
    cubey::procedural::NoiseSource2D source{
        .backend = cubey::procedural::NoiseSource2DBackend::LegacyFbm,
        .output = cubey::procedural::NoiseSource2DOutput::Signed,
        .seed = 31U,
        .legacy_fbm = {.octaves = 4},
        .domain = {.x_scale = 1.25F, .y_scale = 0.75F, .x_offset = 2.0F, .y_offset = -1.0F},
        .warp =
            {
                .enabled = true,
                .seed_offset = 19U,
                .coherent =
                    {
                        .frequency = 0.08F,
                        .warp_type = cubey::procedural::CoherentDomainWarpType::OpenSimplex2,
                        .fractal_type =
                            cubey::procedural::CoherentDomainWarpFractalType::Progressive,
                        .octaves = 2,
                        .lacunarity = 2.0F,
                        .gain = 0.5F,
                        .amplitude = 3.0F,
                    },
            },
    };

    const float sx = (4.0F * 1.25F) + 2.0F;
    const float sy = (-2.0F * 0.75F) - 1.0F;
    cubey::procedural::CoherentDomainWarpConfig warp = source.warp.coherent;
    warp.seed = static_cast<std::int32_t>((source.seed + source.warp.seed_offset) & 0x7fff'ffffULL);
    const cubey::procedural::CoherentWarp2D warped =
        cubey::procedural::domain_warp_2d(sx, sy, warp);
    const float expected =
        cubey::procedural::fbm_2d(warped.x, warped.y, source.seed, {.octaves = 4});

    const float first = cubey::procedural::sample_noise_source_2d(4.0F, -2.0F, source);
    const float second = cubey::procedural::sample_noise_source_2d(4.0F, -2.0F, source);
    require_near(first, expected, 0.000001F, "warped source should sample after domain warp");
    require_near(first, second, 0.000001F, "warped source should remain deterministic");

    source.warp.enabled = false;
    const float unwarped = cubey::procedural::sample_noise_source_2d(4.0F, -2.0F, source);
    require_near(unwarped, cubey::procedural::fbm_2d(sx, sy, source.seed, {.octaves = 4}),
                 0.000001F, "disabled warp should preserve unwarped sampling");
    require(std::fabs(first - unwarped) > 0.000001F,
            "enabled warp should alter the sampled coordinates");
}

void test_procedural_source_fields_fill_scalar_fields() {
    const cubey::procedural::Grid2DDesc desc{
        .width = 2,
        .height = 2,
        .cell_size = 2.0F,
        .origin_x = 10.0F,
        .origin_y = -4.0F,
    };
    const cubey::procedural::NoiseSource2D source{
        .backend = cubey::procedural::NoiseSource2DBackend::LegacyFbm,
        .output = cubey::procedural::NoiseSource2DOutput::Signed,
        .seed = 17U,
        .legacy_fbm = {.octaves = 3},
        .domain = {.x_scale = 0.25F, .y_scale = 0.5F, .x_offset = 1.0F, .y_offset = -1.0F},
    };
    const cubey::procedural::ScalarField2D field =
        cubey::procedural::sample_noise_source_field_2d(desc, source);

    require(field.desc().width == desc.width && field.desc().height == desc.height,
            "source field sampling should preserve grid dimensions");
    const float x = cubey::procedural::grid_sample_x(desc, 1U);
    const float y = cubey::procedural::grid_sample_y(desc, 0U);
    require_near(field.at(1U, 0U), cubey::procedural::sample_noise_source_2d(x, y, source),
                 0.000001F, "source field sampling should use centered grid coordinates");
}
