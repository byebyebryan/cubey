#include <cubey/terrain/terrain_surface_field.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubey::terrain {
namespace {
float smooth(float a, float b, float v) {
    const float t = std::clamp((v - a) / (b - a), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

// O(samples) staged filtering; truncation is only at immutable SOURCE bounds.
std::vector<float> average(const std::vector<float>& values, unsigned w, unsigned h, unsigned rx,
                           unsigned rz) {
    std::vector<float> horizontal(values.size()), result(values.size());
    for (unsigned z = 0; z < h; ++z) {
        double sum = 0;
        unsigned lo = 0, hi = 0;
        for (unsigned x = 0; x < w; ++x) {
            const unsigned end = std::min(w, x + rx + 1), begin = x > rx ? x - rx : 0;
            while (hi < end)
                sum += values[std::size_t(z) * w + hi++];
            while (lo < begin)
                sum -= values[std::size_t(z) * w + lo++];
            horizontal[std::size_t(z) * w + x] = float(sum / double(hi - lo));
        }
    }
    for (unsigned x = 0; x < w; ++x) {
        double sum = 0;
        unsigned lo = 0, hi = 0;
        for (unsigned z = 0; z < h; ++z) {
            const unsigned end = std::min(h, z + rz + 1), begin = z > rz ? z - rz : 0;
            while (hi < end)
                sum += horizontal[std::size_t(hi++) * w + x];
            while (lo < begin)
                sum -= horizontal[std::size_t(lo++) * w + x];
            result[std::size_t(z) * w + x] = float(sum / double(hi - lo));
        }
    }
    return result;
}

// Fixed source-coordinate patch noise, decorrelated lattice values; NOT added
// to imported climate/elevation. Its sole role is bounded material breakup.
float noise(math::Vec2 p, std::uint64_t seed) {
    const auto hash = [seed](int x, int z) {
        std::uint32_t v = std::uint32_t(x) * 0x9e3779b9U ^ std::uint32_t(z) * 0x85ebca6bU ^
                          std::uint32_t(seed) ^ std::uint32_t(seed >> 32U);
        v ^= v >> 16U;
        v *= 0x7feb352dU;
        v ^= v >> 15U;
        v *= 0x846ca68bU;
        v ^= v >> 16U;
        return float(v & 0xffffffU) / float(0xffffffU);
    };
    if (std::abs(p.x) > 1e9F || std::abs(p.y) > 1e9F)
        throw std::runtime_error("surface patch coordinate exceeds integer lattice domain");
    const int x = int(std::floor(p.x)), z = int(std::floor(p.y));
    const float tx = smooth(0, 1, p.x - float(x)), tz = smooth(0, 1, p.y - float(z));
    return std::lerp(std::lerp(hash(x, z), hash(x + 1, z), tx),
                     std::lerp(hash(x, z + 1), hash(x + 1, z + 1), tx), tz);
}
} // namespace

math::Vec4 TerrainSurfaceField::sample(math::Vec2 p) const {
    const auto span = bounds.maximum_xz - bounds.minimum_xz;
    if (width < 2 || height < 2 || width > 1024 || height > 1024 ||
        !std::isfinite(bounds.minimum_xz.x) || !std::isfinite(bounds.minimum_xz.y) ||
        !std::isfinite(span.x) || !std::isfinite(span.y) || span.x <= 0 || span.y <= 0 ||
        weights.size() != std::size_t(width) * height || !std::isfinite(p.x) ||
        !std::isfinite(p.y) || p.x < bounds.minimum_xz.x || p.y < bounds.minimum_xz.y ||
        p.x > bounds.maximum_xz.x || p.y > bounds.maximum_xz.y)
        throw std::runtime_error("surface sample is outside its source product");
    const math::Vec2 grid = (p - bounds.minimum_xz) / (bounds.maximum_xz - bounds.minimum_xz) *
                            math::Vec2(float(width - 1), float(height - 1));
    const unsigned x = std::min(unsigned(grid.x), width - 1),
                   z = std::min(unsigned(grid.y), height - 1);
    const unsigned x1 = std::min(x + 1, width - 1), z1 = std::min(z + 1, height - 1);
    const auto at = [&](unsigned xx, unsigned zz) { return weights[std::size_t(zz) * width + xx]; };
    return glm::mix(glm::mix(at(x, z), at(x1, z), grid.x - float(x)),
                    glm::mix(at(x, z1), at(x1, z1), grid.x - float(x)), grid.y - float(z));
}

TerrainSurfaceField make_terrain_surface_field(const asset::TerrainHeightSource& source,
                                               const asset::TerrainHeightSourceBounds& bounds,
                                               const asset::TerrainRasterClimateSource* climate,
                                               std::uint32_t extent) {
    const auto metadata = source.metadata();
    asset::validate_terrain_height_source_metadata(metadata);
    asset::validate_terrain_height_source_bounds(bounds);
    const auto* raster = dynamic_cast<const asset::TerrainRasterHeightSource*>(&source);
    if (raster) {
        if (bounds.minimum_xz != raster->bounds().minimum_xz ||
            bounds.maximum_xz != raster->bounds().maximum_xz)
            throw std::runtime_error("surface product requires full raster source bounds");
        if (climate)
            asset::validate_terrain_climate_binding(*raster, *climate);
    } else if (climate) {
        throw std::runtime_error("climate surface requires a SHA-bound raster source");
    }
    const auto span = bounds.maximum_xz - bounds.minimum_xz;
    if (extent < 2 || extent > 1024 || !std::isfinite(span.x) || !std::isfinite(span.y) ||
        span.x <= 0 || span.y <= 0 || !std::isfinite(metadata.base_height_m) ||
        !std::isfinite(metadata.relief_scale_m) || metadata.relief_scale_m <= 0)
        throw std::runtime_error("invalid source-coordinate surface product");
    const auto samples = [&](float length) {
        return unsigned(std::clamp(std::floor(double(length) / metadata.gradient_step_m) + 1.0, 2.0,
                                   double(extent)));
    };
    const unsigned width = samples(span.x), height = samples(span.y);
    TerrainSurfaceField field{.bounds = bounds, .width = width, .height = height};
    const auto spacing = span / math::Vec2(float(width - 1), float(height - 1));
    std::vector<float> bed(std::size_t(width) * height);
    for (unsigned z = 0; z < height; ++z)
        for (unsigned x = 0; x < width; ++x) {
            const auto p = bounds.minimum_xz + math::Vec2(float(x), float(z)) * spacing;
            const float h = source.sample_height({.world_xz = p});
            if (!std::isfinite(h))
                throw std::runtime_error("nonfinite surface source height");
            bed[std::size_t(z) * width + x] = h;
        }
    const auto filtered = [&](float radius) {
        // Floor-quantized to source-product samples, effective radii reported by
        // callers. No minimum-one expansion on small sources.
        return average(bed, width, height, unsigned(std::min(float(width), radius / spacing.x)),
                       unsigned(std::min(float(height), radius / spacing.y)));
    };
    const auto fine = filtered(90), local = filtered(360), broad = filtered(1440);
    field.weights.reserve(bed.size());
    for (unsigned z = 0; z < height; ++z)
        for (unsigned x = 0; x < width; ++x) {
            const auto i = std::size_t(z) * width + x;
            const unsigned x0 = x == 0 ? 0 : x - 1, x1 = std::min(x + 1, width - 1);
            const unsigned z0 = z == 0 ? 0 : z - 1, z1 = std::min(z + 1, height - 1);
            const float gx =
                (fine[std::size_t(z) * width + x1] - fine[std::size_t(z) * width + x0]) /
                (float(x1 - x0) * spacing.x);
            const float gz =
                (fine[std::size_t(z1) * width + x] - fine[std::size_t(z0) * width + x]) /
                (float(z1 - z0) * spacing.y);
            const float slope = std::sqrt(gx * gx + gz * gz);
            const float concavity = .35F * (local[i] - fine[i]) + .65F * (broad[i] - fine[i]);
            const auto p = bounds.minimum_xz + math::Vec2(float(x), float(z)) * spacing;
            const float patches = .65F * noise(p / 480.0F, metadata.seed) +
                                  .35F * noise(p / 150.0F, metadata.seed ^ 0x17ac32U);
            const float exposure =
                smooth(.22F, .82F, slope) * (1.0F - .55F * smooth(20, 120, concavity));
            float rock = smooth(.23F, .72F, exposure + .28F * (patches - .5F));
            const float shelter =
                (1.0F - smooth(.16F, .58F, slope)) * (.45F + .55F * smooth(-10, 110, concavity));
            float moisture = .2F + .5F * shelter, snow = 0, cover = .6F * shelter;
            if (climate) {
                if (!climate->contains(p))
                    throw std::runtime_error("climate lacks full surface halo");
                const auto potential = terrain_climate_potential(climate->sample(p));
                moisture = potential.moisture_weight;
                cover = shelter * potential.cover_weight * potential.thermal_growth;
                const float elevation = (bed[i] - metadata.base_height_m) / metadata.relief_scale_m;
                snow = potential.annual_cold_potential * potential.wet_snow_potential *
                       smooth(.18F, .55F, elevation) * (1.0F - smooth(.35F, .95F, slope));
            }
            rock *= 1.0F - snow;
            cover = std::min(cover, 1.0F - rock - snow);
            field.weights.emplace_back(rock, cover, moisture, snow);
        }
    return field;
}
} // namespace cubey::terrain
