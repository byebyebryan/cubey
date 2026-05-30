#include "procedural_terrain_fields.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cubey::projects::procedural_terrain {
namespace {

constexpr float kDistanceInfinity = 1.0e20F;
constexpr float kTau = 6.28318530718F;

struct Point2 {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] float saturate(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
    const float t = saturate((value - edge0) / (edge1 - edge0));
    return t * t * (3.0F - (2.0F * t));
}

[[nodiscard]] float lerp(float a, float b, float t) {
    return a + ((b - a) * t);
}

[[nodiscard]] std::uint32_t hash_u32(std::int32_t x, std::int32_t y, std::uint64_t seed) {
    std::uint64_t value = seed;
    value ^= static_cast<std::uint32_t>(x) + 0x9e37'79b9U + (value << 6U) + (value >> 2U);
    value ^= static_cast<std::uint32_t>(y) + 0x85eb'ca6bU + (value << 6U) + (value >> 2U);
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] float random01(std::uint64_t seed, std::uint32_t index, std::uint32_t channel) {
    constexpr float kScale = 1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(hash_u32(static_cast<std::int32_t>(index),
                                       static_cast<std::int32_t>(channel), seed)) *
           kScale;
}

[[nodiscard]] float value_noise(float x, float y, std::uint64_t seed) {
    const float floor_x = std::floor(x);
    const float floor_y = std::floor(y);
    const auto x0 = static_cast<std::int32_t>(floor_x);
    const auto y0 = static_cast<std::int32_t>(floor_y);
    const float tx = smoothstep(0.0F, 1.0F, x - floor_x);
    const float ty = smoothstep(0.0F, 1.0F, y - floor_y);

    const auto corner = [seed](std::int32_t ix, std::int32_t iy) {
        constexpr float kScale =
            1.0F / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
        return (static_cast<float>(hash_u32(ix, iy, seed)) * kScale * 2.0F) - 1.0F;
    };

    const float a = lerp(corner(x0, y0), corner(x0 + 1, y0), tx);
    const float b = lerp(corner(x0, y0 + 1), corner(x0 + 1, y0 + 1), tx);
    return lerp(a, b, ty);
}

[[nodiscard]] float fbm(float x, float y, std::uint64_t seed, std::uint32_t octaves) {
    float frequency = 1.0F;
    float amplitude = 0.5F;
    float sum = 0.0F;
    float weight = 0.0F;
    for (std::uint32_t octave = 0; octave < octaves; ++octave) {
        sum += value_noise(x * frequency, y * frequency, seed + octave * 1013U) * amplitude;
        weight += amplitude;
        frequency *= 2.04F;
        amplitude *= 0.52F;
    }
    return weight == 0.0F ? 0.0F : sum / weight;
}

[[nodiscard]] float ridged(float x, float y, std::uint64_t seed) {
    const float noise = fbm(x, y, seed, 4);
    const float ridge = 1.0F - std::abs(noise);
    return ridge * ridge;
}

[[nodiscard]] float soft_mounds(float x, float y, std::uint64_t seed, std::uint32_t octaves) {
    const float noise = (fbm(x, y, seed, octaves) * 0.5F) + 0.5F;
    return smoothstep(0.32F, 0.92F, noise);
}

[[nodiscard]] float length(Point2 p) {
    return std::sqrt((p.x * p.x) + (p.y * p.y));
}

[[nodiscard]] float dot(Point2 a, Point2 b) {
    return (a.x * b.x) + (a.y * b.y);
}

[[nodiscard]] float distance(Point2 a, Point2 b) {
    return length({a.x - b.x, a.y - b.y});
}

[[nodiscard]] float distance_to_segment(Point2 p, Point2 a, Point2 b) {
    const Point2 ab{b.x - a.x, b.y - a.y};
    const float denom = (ab.x * ab.x) + (ab.y * ab.y);
    const float t =
        denom <= 0.000001F ? 0.0F : saturate((((p.x - a.x) * ab.x) + ((p.y - a.y) * ab.y)) / denom);
    const Point2 q{a.x + (ab.x * t), a.y + (ab.y * t)};
    return distance(p, q);
}

[[nodiscard]] std::size_t grid_index(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    return static_cast<std::size_t>(y) * width + x;
}

[[nodiscard]] Point2 polar_point(float angle, float radius) {
    return {std::cos(angle) * radius, std::sin(angle) * radius};
}

[[nodiscard]] Point2 axis_point(float angle, float axial, float lateral) {
    const Point2 dir{std::cos(angle), std::sin(angle)};
    const Point2 normal{-dir.y, dir.x};
    return {(dir.x * axial) + (normal.x * lateral), (dir.y * axial) + (normal.y * lateral)};
}

[[nodiscard]] float terrain_axis_angle(const TerrainConfig& config) {
    return random01(config.seed, 0U, 103U) * kTau;
}

[[nodiscard]] float terrain_axis_offset(const TerrainConfig& config) {
    return lerp(-0.13F, 0.13F, random01(config.seed, 0U, 107U));
}

[[nodiscard]] float radial_island_radius(const TerrainConfig& config) {
    const float extent_t = saturate((config.land_extent - 0.35F) / (0.90F - 0.35F));
    return lerp(0.48F, 0.86F, extent_t);
}

[[nodiscard]] float angular_bump(float angle, float center, float width) {
    const float delta = std::abs(std::atan2(std::sin(angle - center), std::cos(angle - center)));
    return 1.0F - smoothstep(width * 0.28F, width, delta);
}

[[nodiscard]] float coastline_land_potential(Point2 p, const TerrainConfig& config) {
    const float warp_x = fbm(p.x * 1.45F + 7.2F, p.y * 1.45F - 3.1F, config.seed + 17U, 4) * 0.13F;
    const float warp_y = fbm(p.x * 1.35F - 4.6F, p.y * 1.35F + 8.9F, config.seed + 29U, 4) * 0.13F;
    const Point2 warped{p.x + warp_x, p.y + warp_y};
    const float angle = std::atan2(warped.y, warped.x);
    const float radial_variation =
        1.0F + (std::sin(angle * 2.0F + random01(config.seed, 2U, 3U) * kTau) * 0.08F) +
        (std::sin(angle * 5.0F + random01(config.seed, 5U, 7U) * kTau) * 0.045F);
    float coastline_radius = radial_island_radius(config) * radial_variation;

    for (std::uint32_t index = 0; index < 5U; ++index) {
        const float angle_offset = (random01(config.seed, index, 11U) - 0.5F) * 0.46F;
        const float lobe_angle = (static_cast<float>(index) / 5.0F) * kTau + angle_offset;
        const float width = lerp(0.34F, 0.68F, random01(config.seed, index, 17U));
        const float amplitude = lerp(0.035F, 0.090F, random01(config.seed, index, 19U));
        coastline_radius += angular_bump(angle, lobe_angle, width) * amplitude;
    }

    for (std::uint32_t index = 0; index < 3U; ++index) {
        const float bay_angle = ((static_cast<float>(index) + 0.42F) / 3.0F) * kTau +
                                ((random01(config.seed, index, 23U) - 0.5F) * 0.56F);
        const float width = lerp(0.26F, 0.54F, random01(config.seed, index, 31U));
        const float amplitude = lerp(0.035F, 0.085F, random01(config.seed, index, 37U));
        coastline_radius -= angular_bump(angle, bay_angle, width) * amplitude;
    }
    coastline_radius = std::max(coastline_radius, radial_island_radius(config) * 0.62F);
    const float radius = std::sqrt((warped.x * warped.x * 0.88F) + (warped.y * warped.y * 1.12F));
    const float shape = coastline_radius - radius;

    const float coast_noise =
        (fbm(warped.x * 3.4F + 11.0F, warped.y * 3.4F - 3.0F, config.seed + 41U, 4) * 0.28F) +
        (fbm(warped.x * 9.5F - 7.0F, warped.y * 9.5F + 5.0F, config.seed + 43U, 3) * 0.08F);
    return shape + (coast_noise * config.coast_noise_strength);
}

[[nodiscard]] std::vector<float> smoothed_scalar_field(const std::vector<float>& values,
                                                       std::uint32_t width, std::uint32_t height) {
    std::vector<float> result(values.size());
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            float sum = 0.0F;
            float weight_sum = 0.0F;
            for (std::int32_t oy = -1; oy <= 1; ++oy) {
                const auto sy = static_cast<std::int32_t>(y) + oy;
                if (sy < 0 || sy >= static_cast<std::int32_t>(height)) {
                    continue;
                }
                for (std::int32_t ox = -1; ox <= 1; ++ox) {
                    const auto sx = static_cast<std::int32_t>(x) + ox;
                    if (sx < 0 || sx >= static_cast<std::int32_t>(width)) {
                        continue;
                    }
                    const float weight = ox == 0 && oy == 0   ? 4.0F
                                         : ox == 0 || oy == 0 ? 2.0F
                                                              : 1.0F;
                    sum += values[grid_index(static_cast<std::uint32_t>(sx),
                                             static_cast<std::uint32_t>(sy), width)] *
                           weight;
                    weight_sum += weight;
                }
            }
            result[grid_index(x, y, width)] =
                weight_sum == 0.0F ? values[grid_index(x, y, width)] : sum / weight_sum;
        }
    }
    return result;
}

[[nodiscard]] std::vector<float> smoothed_signed_distance_field(std::vector<float> values,
                                                                std::uint32_t width,
                                                                std::uint32_t height) {
    for (std::uint32_t iteration = 0; iteration < 2U; ++iteration) {
        std::vector<float> next = values;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t sample = grid_index(x, y, width);
                const float center = values[sample];
                const bool land_side = center >= 0.0F;
                float sum = center * 4.0F;
                float weight_sum = 4.0F;
                for (std::int32_t oy = -1; oy <= 1; ++oy) {
                    const auto sy = static_cast<std::int32_t>(y) + oy;
                    if (sy < 0 || sy >= static_cast<std::int32_t>(height)) {
                        continue;
                    }
                    for (std::int32_t ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) {
                            continue;
                        }
                        const auto sx = static_cast<std::int32_t>(x) + ox;
                        if (sx < 0 || sx >= static_cast<std::int32_t>(width)) {
                            continue;
                        }
                        const float neighbor = values[grid_index(
                            static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy), width)];
                        if ((neighbor >= 0.0F) != land_side) {
                            continue;
                        }
                        const float weight = ox == 0 || oy == 0 ? 2.0F : 1.0F;
                        sum += neighbor * weight;
                        weight_sum += weight;
                    }
                }
                const float averaged = sum / weight_sum;
                const float near_shore = 1.0F - smoothstep(96.0F, 260.0F, std::abs(center));
                next[sample] = lerp(center, averaged, near_shore * 0.45F);
            }
        }
        values = std::move(next);
    }
    return values;
}

void remove_single_cell_coast_artifacts(std::vector<bool>& land_mask, std::uint32_t width,
                                        std::uint32_t height) {
    std::vector<bool> next = land_mask;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint32_t land_neighbors = 0;
            std::uint32_t neighbor_count = 0;
            for (std::int32_t oy = -1; oy <= 1; ++oy) {
                const auto sy = static_cast<std::int32_t>(y) + oy;
                if (sy < 0 || sy >= static_cast<std::int32_t>(height)) {
                    continue;
                }
                for (std::int32_t ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const auto sx = static_cast<std::int32_t>(x) + ox;
                    if (sx < 0 || sx >= static_cast<std::int32_t>(width)) {
                        continue;
                    }
                    ++neighbor_count;
                    if (land_mask[grid_index(static_cast<std::uint32_t>(sx),
                                             static_cast<std::uint32_t>(sy), width)]) {
                        ++land_neighbors;
                    }
                }
            }

            const std::size_t sample = grid_index(x, y, width);
            if (land_mask[sample] && land_neighbors <= 2U) {
                next[sample] = false;
            } else if (!land_mask[sample] && neighbor_count >= 5U &&
                       land_neighbors >= neighbor_count - 1U) {
                next[sample] = true;
            }
        }
    }
    land_mask = std::move(next);
}

void smooth_coastline_mask(std::vector<bool>& land_mask, std::uint32_t width,
                           std::uint32_t height) {
    for (std::uint32_t iteration = 0; iteration < 2U; ++iteration) {
        std::vector<bool> next = land_mask;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::uint32_t land_count = 0;
                std::uint32_t sample_count = 0;
                for (std::int32_t oy = -1; oy <= 1; ++oy) {
                    const auto sy = static_cast<std::int32_t>(y) + oy;
                    if (sy < 0 || sy >= static_cast<std::int32_t>(height)) {
                        continue;
                    }
                    for (std::int32_t ox = -1; ox <= 1; ++ox) {
                        const auto sx = static_cast<std::int32_t>(x) + ox;
                        if (sx < 0 || sx >= static_cast<std::int32_t>(width)) {
                            continue;
                        }
                        ++sample_count;
                        if (land_mask[grid_index(static_cast<std::uint32_t>(sx),
                                                 static_cast<std::uint32_t>(sy), width)]) {
                            ++land_count;
                        }
                    }
                }

                const std::size_t sample = grid_index(x, y, width);
                if (land_count * 2U > sample_count) {
                    next[sample] = true;
                } else if (land_count * 2U < sample_count) {
                    next[sample] = false;
                }
            }
        }
        land_mask = std::move(next);
    }
}

[[nodiscard]] float mountain_band(Point2 p, float angle, float center_s, float center_l,
                                  float half_length, float width, float amplitude, float phase,
                                  float weight, std::uint64_t seed) {
    const Point2 dir{std::cos(angle), std::sin(angle)};
    const Point2 normal{-dir.y, dir.x};
    const Point2 center{(dir.x * center_s) + (normal.x * center_l),
                        (dir.y * center_s) + (normal.y * center_l)};
    const Point2 rel{p.x - center.x, p.y - center.y};
    const float axial = dot(rel, dir);
    const float lateral = dot(rel, normal);
    const float t = saturate((axial + half_length) / (half_length * 2.0F));
    const float centerline = (std::sin((t * kTau) + phase) * amplitude) +
                             (std::sin((t * kTau * 2.15F) + (phase * 0.71F)) * amplitude * 0.38F);
    const float dist = std::abs(lateral - centerline);
    const float broad = 1.0F - smoothstep(width * 0.42F, width, dist);
    const float core = 1.0F - smoothstep(width * 0.11F, width * 0.34F, dist);
    const float taper = smoothstep(-half_length - 0.07F, -half_length + 0.18F, axial) *
                        (1.0F - smoothstep(half_length - 0.18F, half_length + 0.07F, axial));
    const float coarse = ridged((p.x * 3.0F) + phase, (p.y * 3.0F) - phase, seed + 11U);
    const float fine = ridged((p.x * 8.4F) - (phase * 0.43F), (p.y * 8.4F) + phase, seed + 23U);
    const float peak_texture = (coarse * 0.66F) + (fine * 0.34F);
    const float peak = smoothstep(0.44F, 0.90F, peak_texture) * (0.42F + (0.58F * core));
    const float shoulder = broad * 0.18F;
    return saturate((shoulder + (broad * peak)) * taper * weight);
}

[[nodiscard]] float ridge_field(Point2 p, const TerrainConfig& config) {
    const float base_angle = terrain_axis_angle(config);
    const float base_offset = terrain_axis_offset(config);
    float strength = mountain_band(
        p, base_angle, lerp(-0.08F, 0.08F, random01(config.seed, 0U, 109U)), base_offset, 0.68F,
        0.24F, lerp(0.055F, 0.125F, random01(config.seed, 0U, 113U)),
        random01(config.seed, 0U, 127U) * kTau, 1.0F, config.seed + 400U);

    for (std::uint32_t index = 0; index < 2U; ++index) {
        const float side = index == 0U ? -1.0F : 1.0F;
        const float angle = base_angle +
                            (side * lerp(0.48F, 1.04F, random01(config.seed, index, 131U))) +
                            ((random01(config.seed, index, 137U) - 0.5F) * 0.24F);
        const float center_s = lerp(-0.46F, 0.46F, random01(config.seed, index, 139U));
        const float center_l =
            base_offset + (side * lerp(0.10F, 0.28F, random01(config.seed, index, 149U)));
        const float branch = mountain_band(
            p, angle, center_s, center_l, lerp(0.24F, 0.42F, random01(config.seed, index, 151U)),
            lerp(0.12F, 0.20F, random01(config.seed, index, 157U)),
            lerp(0.030F, 0.075F, random01(config.seed, index, 163U)),
            random01(config.seed, index, 167U) * kTau,
            lerp(0.38F, 0.62F, random01(config.seed, index, 173U)), config.seed + 500U + index);
        strength = std::max(strength, branch);
    }

    const float rough_highland = ridged(p.x * 2.4F + 3.1F, p.y * 2.4F - 8.7F, config.seed + 577U) *
                                 smoothstep(0.82F, 0.20F, length(p)) * 0.12F;
    strength = std::max(strength, rough_highland);
    return strength;
}

[[nodiscard]] float valley_field(Point2 p, const TerrainConfig& config) {
    const float base_angle = terrain_axis_angle(config);
    const float base_offset = terrain_axis_offset(config);
    float strength = 0.0F;
    for (std::uint32_t index = 0; index < 5U; ++index) {
        const float source_s = lerp(-0.58F, 0.58F, random01(config.seed, index, 601U));
        const float source_l =
            base_offset + lerp(-0.15F, 0.15F, random01(config.seed, index, 607U));
        const Point2 start = axis_point(base_angle, source_s, source_l);
        const float outlet_angle =
            std::atan2(start.y, start.x) + ((random01(config.seed, index, 613U) - 0.5F) * 1.08F);
        const Point2 end =
            polar_point(outlet_angle, lerp(0.70F, 0.96F, random01(config.seed, index, 617U)));
        const Point2 delta{end.x - start.x, end.y - start.y};
        const float delta_length = std::max(length(delta), 0.001F);
        const Point2 side{-delta.y / delta_length, delta.x / delta_length};
        const float bend_t = lerp(0.40F, 0.66F, random01(config.seed, index, 619U));
        const float bend_offset = lerp(-0.16F, 0.16F, random01(config.seed, index, 631U));
        const Point2 bend{start.x + (delta.x * bend_t) + (side.x * bend_offset),
                          start.y + (delta.y * bend_t) + (side.y * bend_offset)};
        const float width = lerp(0.060F, 0.120F, random01(config.seed, index, 641U));
        const float dist =
            std::min(distance_to_segment(p, start, bend), distance_to_segment(p, bend, end));
        const float line = 1.0F - smoothstep(width * 0.16F, width, dist);
        const float downstream = smoothstep(0.06F, 0.24F, distance(p, start));
        const float meander = 0.70F + (0.30F * fbm(p.x * 7.2F + static_cast<float>(index) * 3.0F,
                                                   p.y * 7.2F - static_cast<float>(index) * 2.0F,
                                                   config.seed + 700U + index, 3));
        strength = std::max(strength, line * downstream * meander * 0.58F);
    }
    return strength;
}

[[nodiscard]] std::vector<float> squared_distance_to_mask(const std::vector<bool>& mask,
                                                          std::uint32_t width, std::uint32_t height,
                                                          bool feature_value) {
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float> source(count, kDistanceInfinity);
    std::size_t feature_count = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (mask[index] == feature_value) {
            source[index] = 0.0F;
            ++feature_count;
        }
    }
    if (feature_count == 0) {
        return source;
    }

    auto transform_1d = [](const std::vector<float>& f, std::vector<float>& d) {
        const auto n = static_cast<std::int32_t>(f.size());
        std::vector<std::int32_t> sites;
        sites.reserve(f.size());
        for (std::int32_t index = 0; index < n; ++index) {
            if (f[static_cast<std::size_t>(index)] < kDistanceInfinity * 0.5F) {
                sites.push_back(index);
            }
        }
        if (sites.empty()) {
            std::fill(d.begin(), d.end(), kDistanceInfinity);
            return;
        }

        std::vector<std::int32_t> v(sites.size());
        std::vector<float> z(sites.size() + 1U);
        std::int32_t k = 0;
        v[0] = sites[0];
        z[0] = -std::numeric_limits<float>::infinity();
        z[1] = std::numeric_limits<float>::infinity();
        for (std::size_t site_index = 1; site_index < sites.size(); ++site_index) {
            const std::int32_t q = sites[site_index];
            float s = 0.0F;
            while (true) {
                const std::int32_t p = v[static_cast<std::size_t>(k)];
                s = ((f[static_cast<std::size_t>(q)] + static_cast<float>(q * q)) -
                     (f[static_cast<std::size_t>(p)] + static_cast<float>(p * p))) /
                    static_cast<float>((2 * q) - (2 * p));
                if (s > z[static_cast<std::size_t>(k)]) {
                    break;
                }
                if (k == 0) {
                    k = -1;
                    s = -std::numeric_limits<float>::infinity();
                    break;
                }
                --k;
            }
            ++k;
            v[static_cast<std::size_t>(k)] = q;
            z[static_cast<std::size_t>(k)] = s;
            z[static_cast<std::size_t>(k + 1)] = std::numeric_limits<float>::infinity();
        }
        k = 0;
        for (std::int32_t q = 0; q < n; ++q) {
            while (z[static_cast<std::size_t>(k + 1)] < static_cast<float>(q)) {
                ++k;
            }
            const std::int32_t p = v[static_cast<std::size_t>(k)];
            const float delta = static_cast<float>(q - p);
            d[static_cast<std::size_t>(q)] = (delta * delta) + f[static_cast<std::size_t>(p)];
        }
    };

    std::vector<float> tmp(count);
    std::vector<float> row_source(width);
    std::vector<float> row_distance(width);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t row_offset = static_cast<std::size_t>(y) * width;
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(row_offset), width,
                    row_source.begin());
        transform_1d(row_source, row_distance);
        std::copy(row_distance.begin(), row_distance.end(),
                  tmp.begin() + static_cast<std::ptrdiff_t>(row_offset));
    }

    std::vector<float> result(count);
    std::vector<float> column_source(height);
    std::vector<float> column_distance(height);
    for (std::uint32_t x = 0; x < width; ++x) {
        for (std::uint32_t y = 0; y < height; ++y) {
            column_source[y] = tmp[static_cast<std::size_t>(y) * width + x];
        }
        transform_1d(column_source, column_distance);
        for (std::uint32_t y = 0; y < height; ++y) {
            result[static_cast<std::size_t>(y) * width + x] = column_distance[y];
        }
    }
    return result;
}

[[nodiscard]] TerrainMaterialMask normalized_mask(float sand, float rock, float vegetation,
                                                  float sediment, bool underwater) {
    sand = std::max(sand, 0.0F);
    rock = std::max(rock, 0.0F);
    vegetation = std::max(vegetation, 0.0F);
    sediment = std::max(sediment, 0.0F);
    float sum = sand + rock + vegetation + sediment;
    if (sum <= 0.0001F) {
        sediment = underwater ? 1.0F : 0.0F;
        sand = underwater ? 0.0F : 1.0F;
        rock = 0.0F;
        vegetation = 0.0F;
        sum = 1.0F;
    }
    return {
        .sand = sand / sum,
        .rock = rock / sum,
        .vegetation = vegetation / sum,
        .sediment = sediment / sum,
    };
}

void relax_land_heights(TerrainFieldData& fields, const TerrainConfig& config) {
    std::vector<float> relaxed = fields.height_m;
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            if (fields.height_m[sample] <= fields.desc.sea_level_m) {
                continue;
            }

            float sum = fields.height_m[sample] * 4.0F;
            float weight_sum = 4.0F;
            for (std::int32_t oy = -1; oy <= 1; ++oy) {
                const auto sy = static_cast<std::int32_t>(y) + oy;
                if (sy < 0 || sy >= static_cast<std::int32_t>(fields.desc.height)) {
                    continue;
                }
                for (std::int32_t ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) {
                        continue;
                    }
                    const auto sx = static_cast<std::int32_t>(x) + ox;
                    if (sx < 0 || sx >= static_cast<std::int32_t>(fields.desc.width)) {
                        continue;
                    }
                    const std::size_t neighbor = fields.index(static_cast<std::uint32_t>(sx),
                                                              static_cast<std::uint32_t>(sy));
                    if (fields.height_m[neighbor] <= fields.desc.sea_level_m) {
                        continue;
                    }
                    const float weight = ox == 0 || oy == 0 ? 2.0F : 1.0F;
                    sum += fields.height_m[neighbor] * weight;
                    weight_sum += weight;
                }
            }

            const float local_average = sum / weight_sum;
            const float ridge = saturate(fields.ridge_strength[sample]);
            const float shore_guard = smoothstep(20.0F, 92.0F, fields.shore_sdf_m[sample]);
            const float local_cap =
                local_average + ((22.0F + (62.0F * ridge)) * config.relief_scale);
            const float capped = std::min(fields.height_m[sample], local_cap);
            const float relax_amount = (0.10F + (0.10F * ridge)) * shore_guard;
            const float coast = smoothstep(0.0F, 76.0F, fields.shore_sdf_m[sample]);
            const float floor = fields.desc.sea_level_m + 0.12F + (coast * 1.6F);
            relaxed[sample] = std::max(lerp(capped, local_average, relax_amount), floor);
            fields.height_contributions[sample].relax_delta_m =
                relaxed[sample] - fields.height_m[sample];
        }
    }

    fields.height_m = std::move(relaxed);
    fields.min_height_m = std::numeric_limits<float>::max();
    fields.max_height_m = std::numeric_limits<float>::lowest();
    fields.max_water_depth_m = 0.0F;
    for (std::size_t index = 0; index < fields.sample_count(); ++index) {
        fields.water_depth_m[index] =
            std::max(0.0F, fields.desc.sea_level_m - fields.height_m[index]);
        fields.max_water_depth_m = std::max(fields.max_water_depth_m, fields.water_depth_m[index]);
        fields.min_height_m = std::min(fields.min_height_m, fields.height_m[index]);
        fields.max_height_m = std::max(fields.max_height_m, fields.height_m[index]);
    }
}

} // namespace

std::size_t TerrainFieldData::sample_count() const {
    return static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height);
}

std::size_t TerrainFieldData::index(std::uint32_t x, std::uint32_t y) const {
    if (x >= desc.width || y >= desc.height) {
        throw std::runtime_error("terrain field index is out of bounds");
    }
    return static_cast<std::size_t>(y) * desc.width + x;
}

TerrainFieldData generate_terrain_fields(const TerrainConfig& config) {
    validate_terrain_config(config);

    TerrainFieldData fields;
    fields.desc = {
        .seed = config.seed,
        .width = config.grid_width,
        .height = config.grid_height,
        .cell_size_m = config.cell_size_m,
        .sea_level_m = config.sea_level_m,
    };
    const std::size_t count = fields.sample_count();
    fields.height_m.resize(count);
    fields.water_depth_m.resize(count);
    fields.shore_sdf_m.resize(count);
    fields.slope.resize(count);
    fields.material_masks.resize(count);
    fields.height_contributions.resize(count);
    fields.land_potential.resize(count);
    fields.inland.resize(count);
    fields.ridge_strength.resize(count);
    fields.valley_strength.resize(count);

    std::vector<bool> land_mask(count, false);
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        const float v = fields.desc.height == 1U
                            ? 0.0F
                            : (static_cast<float>(y) / static_cast<float>(fields.desc.height - 1U));
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const float u =
                fields.desc.width == 1U
                    ? 0.0F
                    : (static_cast<float>(x) / static_cast<float>(fields.desc.width - 1U));
            const Point2 p{(u * 2.0F) - 1.0F, (v * 2.0F) - 1.0F};
            const std::size_t sample = fields.index(x, y);
            fields.land_potential[sample] = coastline_land_potential(p, config);
        }
    }

    const std::vector<float> coast_smoothed =
        smoothed_scalar_field(fields.land_potential, fields.desc.width, fields.desc.height);
    for (std::size_t index = 0; index < count; ++index) {
        fields.land_potential[index] =
            (fields.land_potential[index] * 0.72F) + (coast_smoothed[index] * 0.28F);
        land_mask[index] = fields.land_potential[index] >= 0.0F;
    }
    remove_single_cell_coast_artifacts(land_mask, fields.desc.width, fields.desc.height);
    remove_single_cell_coast_artifacts(land_mask, fields.desc.width, fields.desc.height);
    smooth_coastline_mask(land_mask, fields.desc.width, fields.desc.height);

    std::size_t land_count = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (land_mask[index]) {
            fields.land_potential[index] = std::max(fields.land_potential[index], 0.001F);
            ++land_count;
        } else {
            fields.land_potential[index] = std::min(fields.land_potential[index], -0.001F);
        }
    }
    if (land_count == 0U || land_count == count) {
        throw std::runtime_error("terrain generator produced degenerate land coverage");
    }

    const std::vector<float> squared_to_water =
        squared_distance_to_mask(land_mask, fields.desc.width, fields.desc.height, false);
    const std::vector<float> squared_to_land =
        squared_distance_to_mask(land_mask, fields.desc.width, fields.desc.height, true);

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::size_t sample = fields.index(x, y);
            const bool land = land_mask[sample];
            const float shore_distance_cells = std::sqrt(
                std::max(0.0F, land ? squared_to_water[sample] : squared_to_land[sample]));
            fields.shore_sdf_m[sample] =
                (land ? shore_distance_cells : -shore_distance_cells) * fields.desc.cell_size_m;
        }
    }
    fields.shore_sdf_m = smoothed_signed_distance_field(std::move(fields.shore_sdf_m),
                                                        fields.desc.width, fields.desc.height);
    fields.max_abs_shore_sdf_m = 0.0F;
    for (const float shore_sdf : fields.shore_sdf_m) {
        fields.max_abs_shore_sdf_m = std::max(fields.max_abs_shore_sdf_m, std::abs(shore_sdf));
    }

    fields.min_height_m = std::numeric_limits<float>::max();
    fields.max_height_m = std::numeric_limits<float>::lowest();
    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        const float v = fields.desc.height == 1U
                            ? 0.0F
                            : (static_cast<float>(y) / static_cast<float>(fields.desc.height - 1U));
        const float z =
            (static_cast<float>(y) - (static_cast<float>(fields.desc.height - 1U) * 0.5F)) *
            fields.desc.cell_size_m;
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const float u =
                fields.desc.width == 1U
                    ? 0.0F
                    : (static_cast<float>(x) / static_cast<float>(fields.desc.width - 1U));
            const Point2 p{(u * 2.0F) - 1.0F, (v * 2.0F) - 1.0F};
            const float world_x =
                (static_cast<float>(x) - (static_cast<float>(fields.desc.width - 1U) * 0.5F)) *
                fields.desc.cell_size_m;
            const std::size_t sample = fields.index(x, y);
            const bool land = land_mask[sample];
            const float shore_sdf = fields.shore_sdf_m[sample];

            const float inland = land ? smoothstep(10.0F, 260.0F, shore_sdf) : 0.0F;
            fields.inland[sample] = inland;
            const float raw_ridge =
                land ? ridge_field(p, config) * smoothstep(44.0F, 230.0F, shore_sdf) : 0.0F;
            const float ridge_gate = smoothstep(0.16F, 0.78F, raw_ridge);
            fields.ridge_strength[sample] =
                raw_ridge * ridge_gate * std::clamp(config.ridge_scale, 0.0F, 2.0F);
            fields.valley_strength[sample] = land ? valley_field(p, config) *
                                                        smoothstep(20.0F, 210.0F, shore_sdf) *
                                                        std::clamp(config.valley_scale, 0.0F, 2.0F)
                                                  : 0.0F;

            const float nx = world_x / 360.0F;
            const float nz = z / 360.0F;
            float height = fields.desc.sea_level_m;
            TerrainHeightContributions contributions{};
            if (land) {
                const float coast = smoothstep(0.0F, 76.0F, shore_sdf);
                contributions.coast_lift_m = coast * 8.5F * config.relief_scale;
                contributions.inland_lift_m = std::pow(inland, 0.68F) * 42.0F * config.relief_scale;
                contributions.broad_noise_m =
                    fbm(nx * 1.55F + 1.3F, nz * 1.55F - 8.1F, config.seed + 101U, 5) * 18.0F *
                    config.relief_scale * (0.28F + (0.72F * inland));
                contributions.detail_noise_m =
                    fbm(nx * 5.2F - 6.0F, nz * 5.2F + 4.0F, config.seed + 151U, 4) * 4.8F *
                    config.relief_scale * inland;
                contributions.foothills_m =
                    soft_mounds(nx * 1.85F - 2.5F, nz * 1.85F + 6.2F, config.seed + 233U, 5) *
                    9.0F * config.relief_scale * inland *
                    (1.0F - (saturate(fields.ridge_strength[sample]) * 0.48F));
                contributions.ridge_m = std::pow(saturate(fields.ridge_strength[sample]), 1.58F) *
                                        66.0F * config.relief_scale;
                contributions.broken_ridge_m =
                    soft_mounds(nx * 3.1F + 8.0F, nz * 3.1F - 5.0F, config.seed + 211U, 4) * 8.0F *
                    config.relief_scale * saturate(fields.ridge_strength[sample] * 0.72F);
                contributions.valley_cut_m = fields.valley_strength[sample] *
                                             (3.0F + (18.0F * inland)) * config.relief_scale;
                height = fields.desc.sea_level_m + 0.22F + contributions.coast_lift_m +
                         contributions.inland_lift_m + contributions.broad_noise_m +
                         contributions.detail_noise_m + contributions.foothills_m +
                         contributions.ridge_m + contributions.broken_ridge_m -
                         contributions.valley_cut_m;
                height = std::max(height, fields.desc.sea_level_m + 0.12F + (coast * 1.6F));
            } else {
                const float depth_distance = -shore_sdf;
                const float near = smoothstep(0.0F, 104.0F, depth_distance);
                const float shelf = smoothstep(96.0F, 420.0F, depth_distance);
                const float abyss = smoothstep(420.0F, 920.0F, depth_distance);
                const float seabed =
                    fbm(nx * 0.86F - 3.0F, nz * 0.86F + 5.0F, config.seed + 307U, 4) *
                    (3.2F + (shelf * 7.4F));
                const float depth =
                    0.25F + (near * 10.0F) + (shelf * 48.0F) + (abyss * 128.0F) + seabed;
                height = fields.desc.sea_level_m - std::max(depth, 0.5F);
            }

            contributions.pre_relax_height_m = height;
            fields.height_contributions[sample] = contributions;
            fields.height_m[sample] = height;
            fields.water_depth_m[sample] = std::max(0.0F, fields.desc.sea_level_m - height);
            fields.max_water_depth_m =
                std::max(fields.max_water_depth_m, fields.water_depth_m[sample]);
            fields.min_height_m = std::min(fields.min_height_m, height);
            fields.max_height_m = std::max(fields.max_height_m, height);
        }
    }

    relax_land_heights(fields, config);

    for (std::uint32_t y = 0; y < fields.desc.height; ++y) {
        for (std::uint32_t x = 0; x < fields.desc.width; ++x) {
            const std::uint32_t left = x == 0U ? x : x - 1U;
            const std::uint32_t right = std::min(x + 1U, fields.desc.width - 1U);
            const std::uint32_t down = y == 0U ? y : y - 1U;
            const std::uint32_t up = std::min(y + 1U, fields.desc.height - 1U);
            const float dx = static_cast<float>(right - left) * fields.desc.cell_size_m;
            const float dz = static_cast<float>(up - down) * fields.desc.cell_size_m;
            const float dhdx =
                (fields.height_m[fields.index(right, y)] - fields.height_m[fields.index(left, y)]) /
                std::max(dx, fields.desc.cell_size_m);
            const float dhdz =
                (fields.height_m[fields.index(x, up)] - fields.height_m[fields.index(x, down)]) /
                std::max(dz, fields.desc.cell_size_m);
            const std::size_t sample = fields.index(x, y);
            const float slope = std::sqrt((dhdx * dhdx) + (dhdz * dhdz));
            fields.slope[sample] = slope;

            const float height = fields.height_m[sample] - fields.desc.sea_level_m;
            const float water_depth = fields.water_depth_m[sample];
            const float shore = std::abs(fields.shore_sdf_m[sample]);
            const bool underwater = water_depth > 0.0F;
            const float low_slope = 1.0F - smoothstep(0.10F, 0.46F, slope);
            const float steep = smoothstep(0.28F, 0.90F, slope);
            const float high = smoothstep(60.0F, 128.0F, height);
            const float beach_lowland = (1.0F - smoothstep(10.0F, 34.0F, height)) * low_slope;
            const float immediate_shore =
                (1.0F - smoothstep(0.0F, 24.0F, shore)) * low_slope * 0.35F;
            const float beach_band = saturate(std::max(beach_lowland, immediate_shore));
            const float valley_moisture = saturate(fields.valley_strength[sample] * 1.45F);
            const float ridge_exposure =
                std::pow(smoothstep(0.18F, 0.74F, saturate(fields.ridge_strength[sample])), 1.40F);
            const float land = underwater ? 0.0F : 1.0F;
            const float sand = land * beach_band * (1.0F - smoothstep(34.0F, 70.0F, height));
            const float rock =
                land * std::max(std::max(steep * 0.82F, ridge_exposure * 0.40F), high * 0.58F);
            const float vegetation =
                land * (1.0F - (steep * 0.78F)) * (1.0F - (ridge_exposure * 0.72F)) *
                smoothstep(20.0F, 42.0F, height) * (1.0F - smoothstep(118.0F, 170.0F, height)) *
                (0.64F + (0.46F * valley_moisture));
            const float sediment =
                (underwater ? 1.0F : 0.0F) * low_slope * smoothstep(2.0F, 32.0F, water_depth);
            fields.material_masks[sample] =
                normalized_mask(sand, rock, vegetation, sediment, underwater);
        }
    }

    return fields;
}

} // namespace cubey::projects::procedural_terrain
