#include <cubey/procedural/operators.h>

#include <algorithm>

namespace cubey::procedural {

ScalarField2D box_blur_3x3(const ScalarField2D& field) {
    ScalarField2D result(field.desc());
    const Grid2DDesc& desc = field.desc();
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            float sum = 0.0F;
            float weight_sum = 0.0F;
            for (std::int32_t oy = -1; oy <= 1; ++oy) {
                const auto sy = static_cast<std::int32_t>(y) + oy;
                if (sy < 0 || sy >= static_cast<std::int32_t>(desc.height)) {
                    continue;
                }
                for (std::int32_t ox = -1; ox <= 1; ++ox) {
                    const auto sx = static_cast<std::int32_t>(x) + ox;
                    if (sx < 0 || sx >= static_cast<std::int32_t>(desc.width)) {
                        continue;
                    }
                    const float weight = ox == 0 && oy == 0   ? 4.0F
                                         : ox == 0 || oy == 0 ? 2.0F
                                                              : 1.0F;
                    sum +=
                        field.at(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy)) *
                        weight;
                    weight_sum += weight;
                }
            }
            result.at(x, y) = weight_sum == 0.0F ? field.at(x, y) : sum / weight_sum;
        }
    }
    return result;
}

void normalize_to_unit(ScalarField2D& field) {
    const ScalarFieldStats stats = field.summarize();
    if (stats.sample_count == 0 || stats.span <= 0.0F) {
        field.fill(0.0F);
        return;
    }

    for (float& value : field.values()) {
        value = saturate((value - stats.min) / stats.span);
    }
}

} // namespace cubey::procedural
