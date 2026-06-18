#include <cubey/procedural/field_2d.h>

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace cubey::procedural {

std::size_t sample_count(const Grid2DDesc& desc) {
    return static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height);
}

bool same_grid_desc(const Grid2DDesc& lhs, const Grid2DDesc& rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.cell_size == rhs.cell_size &&
           lhs.origin_x == rhs.origin_x && lhs.origin_y == rhs.origin_y;
}

std::size_t grid_index(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + x;
}

std::size_t checked_grid_index(const Grid2DDesc& desc, std::uint32_t x, std::uint32_t y) {
    if (x >= desc.width || y >= desc.height) {
        throw std::runtime_error("procedural grid index is out of bounds");
    }
    return grid_index(x, y, desc.width);
}

float grid_centered_offset(std::uint32_t index, std::uint32_t count, float cell_size) {
    if (index >= count) {
        throw std::runtime_error("procedural grid sample index is out of bounds");
    }
    return (static_cast<float>(index) - (static_cast<float>(count - 1U) * 0.5F)) * cell_size;
}

float grid_sample_x(const Grid2DDesc& desc, std::uint32_t x) {
    return desc.origin_x + grid_centered_offset(x, desc.width, desc.cell_size);
}

float grid_sample_y(const Grid2DDesc& desc, std::uint32_t y) {
    return desc.origin_y + grid_centered_offset(y, desc.height, desc.cell_size);
}

ScalarFieldStats summarize_scalar_field(std::span<const float> values) {
    ScalarFieldStats stats;
    stats.sample_count = values.size();
    if (values.empty()) {
        return stats;
    }

    const auto [min_it, max_it] = std::minmax_element(values.begin(), values.end());
    stats.min = *min_it;
    stats.max = *max_it;
    stats.span = stats.max - stats.min;
    stats.mean =
        std::accumulate(values.begin(), values.end(), 0.0F) / static_cast<float>(values.size());
    return stats;
}

ScalarField2D::ScalarField2D(Grid2DDesc desc, float value) : desc_(desc) {
    if (desc_.width == 0U || desc_.height == 0U) {
        throw std::runtime_error("procedural scalar field dimensions must be non-zero");
    }
    values_.assign(cubey::procedural::sample_count(desc_), value);
}

const Grid2DDesc& ScalarField2D::desc() const {
    return desc_;
}

std::size_t ScalarField2D::sample_count() const {
    return values_.size();
}

std::size_t ScalarField2D::index(std::uint32_t x, std::uint32_t y) const {
    return checked_grid_index(desc_, x, y);
}

float& ScalarField2D::at(std::uint32_t x, std::uint32_t y) {
    return values_.at(index(x, y));
}

float ScalarField2D::at(std::uint32_t x, std::uint32_t y) const {
    return values_.at(index(x, y));
}

std::span<float> ScalarField2D::values() {
    return values_;
}

std::span<const float> ScalarField2D::values() const {
    return values_;
}

ScalarFieldStats ScalarField2D::summarize() const {
    return summarize_scalar_field(values_);
}

void ScalarField2D::fill(float value) {
    std::fill(values_.begin(), values_.end(), value);
}

} // namespace cubey::procedural
