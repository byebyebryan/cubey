#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/noise.h>
#include <cubey/procedural/operators.h>

#include <array>
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

void test_procedural_operators_include_smootherstep() {
    require_near(cubey::procedural::smootherstep01(-1.0F), 0.0F, 0.0001F,
                 "smootherstep should saturate below zero");
    require_near(cubey::procedural::smootherstep01(0.5F), 0.5F, 0.0001F,
                 "smootherstep midpoint should stay centered");
    require_near(cubey::procedural::smootherstep01(2.0F), 1.0F, 0.0001F,
                 "smootherstep should saturate above one");
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
    require_near(cubey::procedural::value_noise_2d(1.25F, -3.75F, 17U), -0.457798481F,
                 0.000001F, "legacy value noise should keep stable samples");
    require_near(cubey::procedural::fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5}), -0.063320771F,
                 0.000001F, "legacy fbm should keep stable samples");
    require_near(cubey::procedural::ridged_fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5}),
                 0.877368033F, 0.000001F, "legacy ridged fbm should keep stable samples");
}

void test_procedural_3d_noise_is_deterministic_and_stable() {
    const float first = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U);
    const float second = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U);
    require_near(first, second, 0.000001F, "3D value noise should be deterministic");
    require(first >= -1.0F && first <= 1.0F,
            "3D value noise should remain in signed unit range");

    const float changed_seed = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 18U);
    require(std::fabs(first - changed_seed) > 0.000001F,
            "3D value noise should vary when the seed changes");

    const float fbm = cubey::procedural::fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5});
    require(fbm >= -1.0F && fbm <= 1.0F, "3D fbm should remain in signed unit range");

    const float ridged =
        cubey::procedural::ridged_fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5});
    require(ridged >= 0.0F && ridged <= 1.0F, "3D ridged fbm should remain in unit range");

    require_near(cubey::procedural::hash_to_unit(123456789U), 0.659940481F, 0.000001F,
                 "3D-compatible hash-to-unit should keep stable samples");
    require(cubey::procedural::hash_u32(-2, 7, 4, 19U) == 2469915974U,
            "3D-compatible hash should keep stable samples");
    require_near(cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U),
                 0.302227825F, 0.000001F,
                 "3D value noise should keep stable samples");
    require_near(cubey::procedural::fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5}),
                 -0.123893112F, 0.000001F, "3D fbm should keep stable samples");
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
    require(std::fabs(warp_a.x - 1.50F) > 0.000001F ||
                std::fabs(warp_a.y + 2.25F) > 0.000001F,
            "2D domain warp should move at least one coordinate");

    CoherentDomainWarpConfig warp3 = warp;
    warp3.warp_type = CoherentDomainWarpType::OpenSimplex2Reduced;
    warp3.fractal_type = CoherentDomainWarpFractalType::Independent;
    const cubey::procedural::CoherentWarp3D warped =
        cubey::procedural::domain_warp_3d(1.0F, 2.0F, 3.0F, warp3);
    require(std::isfinite(warped.x) && std::isfinite(warped.y) && std::isfinite(warped.z),
            "3D domain warp should produce finite coordinates");
}
