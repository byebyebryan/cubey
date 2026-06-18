#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cubey::procedural {

struct Grid2DDesc {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    float cell_size = 1.0F;
    float origin_x = 0.0F;
    float origin_y = 0.0F;
};

struct ScalarFieldStats {
    std::size_t sample_count = 0;
    float min = 0.0F;
    float max = 0.0F;
    float span = 0.0F;
    float mean = 0.0F;
};

[[nodiscard]] std::size_t sample_count(const Grid2DDesc& desc);
[[nodiscard]] bool same_grid_desc(const Grid2DDesc& lhs, const Grid2DDesc& rhs);
[[nodiscard]] std::size_t grid_index(std::uint32_t x, std::uint32_t y, std::uint32_t width);
[[nodiscard]] std::size_t checked_grid_index(const Grid2DDesc& desc, std::uint32_t x,
                                             std::uint32_t y);
[[nodiscard]] float grid_centered_offset(std::uint32_t index, std::uint32_t count, float cell_size);
[[nodiscard]] float grid_sample_x(const Grid2DDesc& desc, std::uint32_t x);
[[nodiscard]] float grid_sample_y(const Grid2DDesc& desc, std::uint32_t y);
[[nodiscard]] ScalarFieldStats summarize_scalar_field(std::span<const float> values);

class ScalarField2D {
  public:
    ScalarField2D() = default;
    explicit ScalarField2D(Grid2DDesc desc, float value = 0.0F);

    [[nodiscard]] const Grid2DDesc& desc() const;
    [[nodiscard]] std::size_t sample_count() const;
    [[nodiscard]] std::size_t index(std::uint32_t x, std::uint32_t y) const;

    [[nodiscard]] float& at(std::uint32_t x, std::uint32_t y);
    [[nodiscard]] float at(std::uint32_t x, std::uint32_t y) const;

    [[nodiscard]] std::span<float> values();
    [[nodiscard]] std::span<const float> values() const;
    [[nodiscard]] ScalarFieldStats summarize() const;

    void fill(float value);

  private:
    Grid2DDesc desc_{};
    std::vector<float> values_{0.0F};
};

} // namespace cubey::procedural
