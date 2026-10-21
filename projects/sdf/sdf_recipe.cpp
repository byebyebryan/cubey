#include "sdf_recipe.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {

class Random {
  public:
    explicit Random(std::uint32_t seed) : state_(static_cast<std::uint64_t>(seed) + 1U) {
        static_cast<void>(next());
    }
    std::uint32_t next() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto bits = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
        return std::rotr(bits, static_cast<int>(old >> 59U));
    }
    float unit() {
        return static_cast<float>(next() >> 8U) * (1.0F / 16777216.0F);
    }
    float range(float low, float high) {
        return low + (high - low) * unit();
    }

  private:
    std::uint64_t state_;
};

bool finite(math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

void validate_edit(const BoxEdit& edit) {
    const auto q = edit.rotation;
    const auto norm_squared = glm::dot(q, q);
    if (!finite(edit.center) || !finite(edit.half_extent) ||
        glm::any(glm::lessThanEqual(edit.half_extent, math::Vec3{0.0F})) ||
        !std::isfinite(edit.rounding) || edit.rounding < 0.0F || !std::isfinite(q.x) ||
        !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w) ||
        !std::isfinite(norm_squared) || norm_squared < 1.0e-12F ||
        (edit.operation != Operation::Fill && edit.operation != Operation::Remove)) {
        throw std::invalid_argument("invalid SDF box edit");
    }
}

GpuEdit gpu_edit(const BoxEdit& edit) {
    validate_edit(edit);
    const auto inverse = glm::conjugate(glm::normalize(edit.rotation));
    return {{edit.center, static_cast<float>(edit.operation)},
            {inverse.x, inverse.y, inverse.z, inverse.w},
            {edit.half_extent, edit.rounding}};
}

std::vector<GpuEdit> gpu_edits(std::span<const BoxEdit> edits) {
    std::vector<GpuEdit> result;
    result.reserve(edits.size());
    for (const auto& edit : edits)
        result.push_back(gpu_edit(edit));
    return result;
}

float box_distance(math::Vec3 point, math::Vec3 half_extent, float rounding) {
    const auto q = glm::abs(point) - half_extent;
    return glm::length(glm::max(q, math::Vec3{0.0F})) + std::min(std::max({q.x, q.y, q.z}), 0.0F) -
           rounding;
}

float edit_distance(math::Vec3 point, const BoxEdit& edit) {
    const auto local = glm::conjugate(glm::normalize(edit.rotation)) * (point - edit.center);
    return std::max(box_distance(local, edit.half_extent, edit.rounding),
                    box_distance(point, math::Vec3{0.499F}, 0.0F));
}

float recipe_distance(math::Vec3 point, std::span<const BoxEdit> edits) {
    float distance = 1000.0F;
    for (const auto& edit : edits) {
        const float next = edit_distance(point, edit);
        distance = edit.operation == Operation::Fill ? std::min(distance, next)
                                                     : std::max(distance, -next);
    }
    return distance;
}

Recipe make_recipe(std::string_view scene, std::uint32_t seed, std::uint32_t count) {
    Recipe recipe;
    if (scene == "box" || scene == "rotated" || scene == "cut") {
        BoxEdit box;
        box.half_extent = {0.24F, 0.19F, 0.22F};
        if (scene != "box")
            box.rotation = math::euler_xyz_quat({0.2F, 0.37F, 0.1F});
        recipe.edits.push_back(box);
        if (scene == "cut") {
            BoxEdit cut;
            cut.center = {0.12F, 0.11F, 0.0F};
            cut.half_extent = {0.17F, 0.14F, 0.4F};
            cut.rotation = math::angle_axis_quat(0.3F, {0, 0, 1});
            cut.operation = Operation::Remove;
            recipe.edits.push_back(cut);
        }
        return recipe;
    }
    if (scene != "sticks" || count > 65536U)
        throw std::invalid_argument(
            "SDF scene must be box, rotated, cut or sticks (count <= 65536)");
    Random random(seed);
    constexpr float tau = 2.0F * std::numbers::pi_v<float>;
    recipe.edits.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const float z = random.range(-1.0F, 1.0F);
        const float angle = random.range(0.0F, tau);
        const float radial = std::sqrt(std::max(0.0F, 1.0F - z * z));
        BoxEdit edit;
        edit.center = math::Vec3{radial * std::cos(angle), z, radial * std::sin(angle)} *
                      random.range(0.29F, 0.3F);
        const float u = random.unit();
        const float a = random.range(0.0F, tau);
        const float b = random.range(0.0F, tau);
        edit.rotation = math::Quat{std::sqrt(u) * std::cos(b), std::sqrt(1.0F - u) * std::sin(a),
                                   std::sqrt(1.0F - u) * std::cos(a), std::sqrt(u) * std::sin(b)};
        edit.half_extent = {random.range(0.05F, 0.2F), 0.001F, 0.001F};
        recipe.edits.push_back(edit);
    }
    return recipe;
}

std::uint64_t recipe_hash(const Recipe& recipe) {
    std::uint64_t hash = 14695981039346656037ULL;
    // Hash canonical GPU lanes in a specified little-endian byte order.
    for (const auto& edit : recipe.edits) {
        const auto packed = gpu_edit(edit);
        for (const auto lane :
             {packed.center_operation, packed.inverse_rotation, packed.extent_rounding}) {
            for (int component = 0; component < 4; ++component) {
                const auto bits = std::bit_cast<std::uint32_t>(lane[component]);
                for (unsigned byte = 0; byte < 4; ++byte) {
                    hash ^= (bits >> (byte * 8U)) & 255U;
                    hash *= 1099511628211ULL;
                }
            }
        }
    }
    return hash;
}

std::size_t construction_prefix(std::size_t count, double seconds, double duration) {
    if (!std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0.0)
        throw std::invalid_argument("SDF construction time must be finite with positive duration");
    return static_cast<std::size_t>(
        std::floor(static_cast<double>(count) * std::clamp(seconds / duration, 0.0, 1.0)));
}

} // namespace cubey::projects::sdf
