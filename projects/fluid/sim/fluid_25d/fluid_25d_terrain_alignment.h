#pragma once

// Private study helpers: sample actual ground triangles, not a new water model.
#include <cubey/terrain/terrain_backdrop_product.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::projects::fluid::fluid_25d::alignment {

inline float bspline(std::span<const float> values, std::uint32_t width, std::uint32_t height,
                     float x, float z) {
    if (width < 2U || height < 2U || values.size() != static_cast<std::size_t>(width) * height ||
        !std::isfinite(x) || !std::isfinite(z)) {
        throw std::runtime_error("invalid B-spline probe field");
    }
    x = std::clamp(x, 0.0F, static_cast<float>(width - 1U));
    z = std::clamp(z, 0.0F, static_cast<float>(height - 1U));
    const auto weights = [](float t) {
        const float t2 = t * t;
        const float t3 = t2 * t;
        return std::array<float, 4>{(1.0F - t) * (1.0F - t) * (1.0F - t) / 6.0F,
                                    (3.0F * t3 - 6.0F * t2 + 4.0F) / 6.0F,
                                    (-3.0F * t3 + 3.0F * t2 + 3.0F * t + 1.0F) / 6.0F, t3 / 6.0F};
    };
    const int ix = static_cast<int>(std::floor(x)) - 1;
    const int iz = static_cast<int>(std::floor(z)) - 1;
    const auto wx = weights(x - std::floor(x));
    const auto wz = weights(z - std::floor(z));
    float result = 0.0F;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const auto cx =
                static_cast<std::size_t>(std::clamp(ix + column, 0, static_cast<int>(width) - 1));
            const auto cz =
                static_cast<std::size_t>(std::clamp(iz + row, 0, static_cast<int>(height) - 1));
            result += values[cz * width + cx] *
                      (wx[static_cast<std::size_t>(column)] * wz[static_cast<std::size_t>(row)]);
        }
    }
    return result;
}

class TriangleSampler {
  public:
    explicit TriangleSampler(float half_extent_m, std::uint32_t bins_per_axis = 128U)
        : half_extent_(half_extent_m), bins_per_axis_(bins_per_axis),
          bins_(checked_bin_count(half_extent_m, bins_per_axis)) {}

    void add(const terrain::TerrainBackdropSectorMesh& mesh) {
        if (mesh.indices.size() % 3U != 0U) {
            throw std::runtime_error("invalid ground triangle index count");
        }
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3U) {
            Triangle triangle{};
            for (std::size_t corner = 0; corner < 3U; ++corner) {
                if (mesh.indices[i + corner] >= mesh.vertices.size()) {
                    throw std::runtime_error("invalid ground triangle index");
                }
                triangle.p[corner] = mesh.vertices[mesh.indices[i + corner]].position;
                for (float value : triangle.p[corner]) {
                    if (!std::isfinite(value)) {
                        throw std::runtime_error("nonfinite ground triangle");
                    }
                }
            }
            const auto& a = triangle.p[0];
            const auto& b = triangle.p[1];
            const auto& c = triangle.p[2];
            const double denominator =
                (static_cast<double>(b[2]) - c[2]) * (static_cast<double>(a[0]) - c[0]) +
                (static_cast<double>(c[0]) - b[0]) * (static_cast<double>(a[2]) - c[2]);
            if (std::abs(denominator) < 1.0e-12) {
                continue;
            }
            triangle.inverse = 1.0 / denominator;
            const float min_x = std::min({a[0], b[0], c[0]});
            const float max_x = std::max({a[0], b[0], c[0]});
            const float min_z = std::min({a[2], b[2], c[2]});
            const float max_z = std::max({a[2], b[2], c[2]});
            if (max_x < -half_extent_ || min_x > half_extent_ || max_z < -half_extent_ ||
                min_z > half_extent_) {
                continue;
            }
            const auto identity = triangles_.size();
            triangles_.push_back(triangle);
            for (auto row = bin(min_z); row <= bin(max_z); ++row) {
                for (auto column = bin(min_x); column <= bin(max_x); ++column) {
                    bins_[static_cast<std::size_t>(row) * bins_per_axis_ + column].push_back(
                        identity);
                }
            }
        }
    }

    [[nodiscard]] std::optional<double> height(float x, float z) const {
        if (!std::isfinite(x) || !std::isfinite(z) || x < -half_extent_ || x > half_extent_ ||
            z < -half_extent_ || z > half_extent_) {
            return std::nullopt;
        }
        for (const auto identity :
             bins_[static_cast<std::size_t>(bin(z)) * bins_per_axis_ + bin(x)]) {
            const auto& triangle = triangles_[identity];
            const auto& a = triangle.p[0];
            const auto& b = triangle.p[1];
            const auto& c = triangle.p[2];
            const double u =
                ((static_cast<double>(b[2]) - c[2]) * (static_cast<double>(x) - c[0]) +
                 (static_cast<double>(c[0]) - b[0]) * (static_cast<double>(z) - c[2])) *
                triangle.inverse;
            const double v =
                ((static_cast<double>(c[2]) - a[2]) * (static_cast<double>(x) - c[0]) +
                 (static_cast<double>(a[0]) - c[0]) * (static_cast<double>(z) - c[2])) *
                triangle.inverse;
            constexpr double tolerance = 1.0e-7;
            if (u >= -tolerance && v >= -tolerance && u + v <= 1.0 + tolerance) {
                return u * a[1] + v * b[1] + (1.0 - u - v) * c[1];
            }
        }
        return std::nullopt;
    }

  private:
    static std::size_t checked_bin_count(float half_extent, std::uint32_t bins) {
        if (!std::isfinite(half_extent) || half_extent <= 0.0F || bins == 0U || bins > 1024U) {
            throw std::runtime_error("invalid terrain probe bounds");
        }
        return static_cast<std::size_t>(bins) * bins;
    }

    struct Triangle {
        std::array<std::array<float, 3>, 3> p{};
        double inverse = 0.0;
    };

    [[nodiscard]] std::uint32_t bin(float value) const {
        const auto scaled =
            (static_cast<double>(value) + half_extent_) / (2.0 * half_extent_) * bins_per_axis_;
        return static_cast<std::uint32_t>(
            std::clamp(scaled, 0.0, static_cast<double>(bins_per_axis_ - 1U)));
    }

    float half_extent_ = 0.0F;
    std::uint32_t bins_per_axis_ = 0U;
    std::vector<Triangle> triangles_{};
    std::vector<std::vector<std::size_t>> bins_{};
};

} // namespace cubey::projects::fluid::fluid_25d::alignment
