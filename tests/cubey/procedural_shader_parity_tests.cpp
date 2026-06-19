#include <cubey/procedural/noise.h>
#include <cubey/procedural/operators.h>

#include <array>
#include <cmath>
#include <cstdint>
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

[[nodiscard]] float mix(float a, float b, float t) {
    return a + ((b - a) * t);
}

[[nodiscard]] float glsl_saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float glsl_remap(float value, float old_min, float old_max, float new_min,
                               float new_max) {
    return new_min + (((value - old_min) / (old_max - old_min)) * (new_max - new_min));
}

[[nodiscard]] float glsl_smoothstep01(float value) {
    const float x = glsl_saturate(value);
    return x * x * (3.0F - (2.0F * x));
}

[[nodiscard]] float glsl_smootherstep01(float value) {
    const float x = glsl_saturate(value);
    return x * x * x * (x * (x * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] std::uint32_t glsl_hash_u32(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float glsl_hash01_u32(std::uint32_t value) {
    return static_cast<float>(glsl_hash_u32(value) & 0x00ff'ffffU) /
           static_cast<float>(0x0100'0000U);
}

[[nodiscard]] std::uint32_t glsl_hash_u32_3d(std::int32_t x, std::int32_t y,
                                             std::int32_t z, std::uint32_t seed) {
    std::uint32_t value = seed;
    value ^= static_cast<std::uint32_t>(x) * 0x9e3779b9U;
    value ^= static_cast<std::uint32_t>(y) * 0x85ebca6bU;
    value ^= static_cast<std::uint32_t>(z) * 0xc2b2ae35U;
    return glsl_hash_u32(value);
}

[[nodiscard]] float glsl_hash01_3d(std::int32_t x, std::int32_t y, std::int32_t z,
                                   std::uint32_t seed) {
    return static_cast<float>(glsl_hash_u32_3d(x, y, z, seed) >> 8U) / 16'777'215.0F;
}

[[nodiscard]] float glsl_value_noise_3d(float x, float y, float z, std::uint32_t seed) {
    const float floor_x = std::floor(x);
    const float floor_y = std::floor(y);
    const float floor_z = std::floor(z);
    const auto x0 = static_cast<std::int32_t>(floor_x);
    const auto y0 = static_cast<std::int32_t>(floor_y);
    const auto z0 = static_cast<std::int32_t>(floor_z);
    const float tx = glsl_smootherstep01(x - floor_x);
    const float ty = glsl_smootherstep01(y - floor_y);
    const float tz = glsl_smootherstep01(z - floor_z);

    const auto corner = [seed](std::int32_t ix, std::int32_t iy, std::int32_t iz) {
        return (glsl_hash01_3d(ix, iy, iz, seed) * 2.0F) - 1.0F;
    };

    const float x00 = mix(corner(x0, y0, z0), corner(x0 + 1, y0, z0), tx);
    const float x10 = mix(corner(x0, y0 + 1, z0), corner(x0 + 1, y0 + 1, z0), tx);
    const float x01 = mix(corner(x0, y0, z0 + 1), corner(x0 + 1, y0, z0 + 1), tx);
    const float x11 = mix(corner(x0, y0 + 1, z0 + 1), corner(x0 + 1, y0 + 1, z0 + 1), tx);
    return mix(mix(x00, x10, ty), mix(x01, x11, ty), tz);
}

[[nodiscard]] float glsl_fbm_3d(float x, float y, float z, std::uint32_t seed,
                                std::uint32_t octaves) {
    float amplitude = 0.5F;
    float frequency = 1.0F;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0U; octave < octaves; ++octave) {
        sum += glsl_value_noise_3d(x * frequency, y * frequency, z * frequency,
                                   seed + octave * 1013U) *
               amplitude;
        weight += amplitude;
        frequency *= 2.03F;
        amplitude *= 0.5F;
    }
    return weight > 0.0F ? sum / weight : 0.0F;
}

} // namespace

void test_procedural_shader_scalar_helpers_match_glsl_contracts() {
    constexpr std::array samples{-1.0F, 0.0F, 0.25F, 0.5F, 0.75F, 1.0F, 2.0F};

    for (const float sample : samples) {
        require_near(cubey::procedural::saturate(sample), glsl_saturate(sample), 0.000001F,
                     "procedural saturate should match GLSL saturate");
        require_near(cubey::procedural::smoothstep01(sample), glsl_smoothstep01(sample),
                     0.000001F, "procedural smoothstep01 should match GLSL smoothstep01");
        require_near(cubey::procedural::smootherstep01(sample), glsl_smootherstep01(sample),
                     0.000001F, "procedural smootherstep01 should match GLSL smootherstep01");
        require_near(cubey::procedural::remap(sample, -2.0F, 6.0F, -1.0F, 3.0F),
                     glsl_remap(sample, -2.0F, 6.0F, -1.0F, 3.0F), 0.000001F,
                     "procedural remap should match GLSL remap for valid ranges");
    }
}

void test_procedural_shader_hash_helpers_match_glsl_contracts() {
    constexpr std::array samples{0U, 1U, 7U, 123456789U, 0xffff'ffffU};

    for (const std::uint32_t sample : samples) {
        require(cubey::procedural::hash_u32(sample) == glsl_hash_u32(sample),
                "procedural uint hash should match GLSL uint hash");
        require_near(cubey::procedural::hash_to_unit_masked_24(sample),
                     glsl_hash01_u32(sample), 0.000001F,
                     "procedural masked hash-to-unit should match GLSL hash01");
    }

    require_near(cubey::procedural::hash_to_unit_masked_24(1U), 0.537364960F, 0.000001F,
                 "masked hash-to-unit should keep stable golden value for one");
    require_near(cubey::procedural::hash_to_unit_masked_24(123456789U), 0.944756031F,
                 0.000001F,
                 "masked hash-to-unit should keep stable golden value for project samples");
    require(std::fabs(cubey::procedural::hash_to_unit(123456789U) -
                      cubey::procedural::hash_to_unit_masked_24(123456789U)) > 0.1F,
            "legacy high-bit hash-to-unit should remain distinct from GLSL masked hash01");
}

void test_procedural_shader_3d_noise_helpers_match_glsl_contracts() {
    struct Sample {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        std::uint32_t seed = 0U;
    };
    constexpr std::array samples{
        Sample{.x = 1.25F, .y = -3.75F, .z = 0.5F, .seed = 17U},
        Sample{.x = 2.4F, .y = -0.7F, .z = 1.9F, .seed = 42U},
        Sample{.x = -0.125F, .y = 9.5F, .z = -4.25F, .seed = 3001U},
    };

    for (const Sample& sample : samples) {
        const auto ix = static_cast<std::int32_t>(std::floor(sample.x));
        const auto iy = static_cast<std::int32_t>(std::floor(sample.y));
        const auto iz = static_cast<std::int32_t>(std::floor(sample.z));
        require(cubey::procedural::hash_u32(ix, iy, iz, sample.seed) ==
                    glsl_hash_u32_3d(ix, iy, iz, sample.seed),
                "procedural 3D hash should match GLSL 3D hash");
        require_near(cubey::procedural::value_noise_3d(sample.x, sample.y, sample.z,
                                                       sample.seed),
                     glsl_value_noise_3d(sample.x, sample.y, sample.z, sample.seed), 0.000001F,
                     "procedural 3D value noise should match GLSL 3D value noise");
        require_near(cubey::procedural::fbm_3d(sample.x, sample.y, sample.z, sample.seed,
                                               {.octaves = 5}),
                     glsl_fbm_3d(sample.x, sample.y, sample.z, sample.seed, 5U), 0.000001F,
                     "procedural 3D FBM should match GLSL 3D FBM");
    }
}
