#include "sdf_live_edit.h"
#include "sdf_regions.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/packing.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
float clip(math::Vec3 p) {
    const auto q = glm::abs(p) - math::Vec3{.499F};
    return glm::length(glm::max(q, math::Vec3{0})) + std::min(std::max({q.x, q.y, q.z}), 0.F);
}
float op(float a, float b, const SceneInstruction& n) {
    const auto code = static_cast<unsigned>(n.center_code.w);
    if (code == 10)
        return std::min(a, b);
    if (code == 11)
        return std::max(a, -b);
    const float k = n.settings.y;
    const float h = std::clamp(.5F + .5F * (code == 13 ? a + b : b - a) / k, 0.F, 1.F);
    return code == 13 ? std::lerp(-b, a, h) + k * h * (1 - h)
                      : std::lerp(b, a, h) - k * h * (1 - h);
}
float raw_prefix(math::Vec3 p, const AuthoredScene& scene) {
    if (scene.program.empty())
        return 1000.F;
    std::array<float, 32> stack{};
    std::size_t size = 0;
    for (const auto& node : scene.program) {
        if (node.center_code.w < 10)
            stack.at(size++) = authored_primitive_distance(p, node);
        else {
            const float b = stack.at(--size), a = stack.at(--size);
            stack.at(size++) = op(a, b, node);
        }
    }
    return stack.at(0);
}
} // namespace
LiveEditProgram prepare_live_edit(AuthoredScene base, AuthoredScene edited, bool source_defined) {
    if (edited.program.size() < base.program.size() ||
        edited.program.size() - base.program.size() > 128 || edited.leaf_count < base.leaf_count ||
        edited.leaf_count - base.leaf_count > 64)
        throw std::invalid_argument(
            "live edit requires at most 64 pending leaves / 128 suffix instructions");
    if (!base.program.empty() && std::memcmp(base.program.data(), edited.program.data(),
                                             base.program.size() * sizeof(SceneInstruction)) != 0)
        throw std::invalid_argument("live edit changed the frozen source prefix");
    const auto base_document = nlohmann::json::parse(serialize_authored_scene(base));
    const auto edited_document = nlohmann::json::parse(serialize_authored_scene(edited));
    if (base_document.at("materials") != edited_document.at("materials"))
        throw std::invalid_argument("live edit requires an unchanged predeclared material palette");
    if (base.environment_only_gallery || edited.environment_only_gallery ||
        std::any_of(edited.optics.begin(), edited.optics.end(),
                    [](const auto& optical) { return !default_optics(optical); }))
        throw std::invalid_argument("live edit is limited to opaque authored atmosphere geometry");
    LiveEditProgram result;
    result.pending_leaves = edited.leaf_count - base.leaf_count;
    result.controls.header = {
        static_cast<std::uint32_t>(base.program.size()),
        static_cast<std::uint32_t>(edited.program.size() - base.program.size()),
        source_defined ? 1U : 0U, 0U};
    result.controls.info.x = authored_evaluation_guard(base);
    const auto bounds = authored_model_bounds(edited);
    result.controls.header[3] = bounds.empty ? 1U : 0U;
    // A sampled base differs from source within active bricks. Expand the
    // combined source bounds by a conservative near-brick error enclosure;
    // pending additions are already included in the canonical edited bounds.
    const float pad = source_defined ? 0.F : .002F + 3.F * result.controls.info.x;
    result.controls.low = {glm::max(bounds.low - pad, math::Vec3{-.5F}), 0.F};
    result.controls.high = {glm::min(bounds.high + pad, math::Vec3{.5F}), 0.F};
    result.base = std::move(base);
    result.edited = std::move(edited);
    return result;
}
float live_edit_cpu_base(math::Vec3 p, const AuthoredScene& base) {
    if (base.program.empty())
        return 1000.F;
    if (glm::any(glm::greaterThan(glm::abs(p), math::Vec3{.5F})))
        return raw_prefix(p, base);
    glm::ivec3 cell{};
    for (unsigned depth = 2; depth <= 8; depth += 2) {
        const float dim = static_cast<float>(1U << depth);
        cell = glm::clamp(glm::ivec3(glm::floor((p + .5F) * dim)), glm::ivec3{0},
                          glm::ivec3{static_cast<int>(dim) - 1});
        const auto center = (math::Vec3{cell} + .5F) / dim - .5F;
        const float d = authored_distance(center, base);
        const float radius = .866025404F / dim + 2e-6F;
        if (std::abs(d) > radius)
            return raw_prefix(p,
                              base); // Uniform cells retain a source extension, not a scalar label.
    }
    const auto local =
        glm::clamp((p + .5F) * 256.F - math::Vec3{cell}, math::Vec3{0}, math::Vec3{1}) * 7.F;
    const auto voxel = glm::min(glm::ivec3(glm::floor(local)), glm::ivec3{6});
    const auto f = local - math::Vec3{voxel};
    std::array<float, 8> values{};
    for (int i = 0; i < 8; ++i) {
        const auto corner = voxel + glm::ivec3{i & 1, (i >> 1) & 1, (i >> 2) & 1};
        const auto q = (math::Vec3{cell} + math::Vec3{corner} / 7.F) / 256.F - .5F;
        values[static_cast<std::size_t>(i)] =
            glm::unpackHalf1x16(glm::packHalf1x16(authored_distance(q, base)));
    }
    const float value = std::lerp(
        std::lerp(std::lerp(values[0], values[1], f.x), std::lerp(values[2], values[3], f.x), f.y),
        std::lerp(std::lerp(values[4], values[5], f.x), std::lerp(values[6], values[7], f.x), f.y),
        f.z);
    const float guard = authored_evaluation_guard(base) + 2e-6F;
    constexpr float h = 1.F / (256.F * 7.F);
    const float error = h * std::sqrt(3.F) * .5F +
                        (std::abs(value) + std::sqrt(3.F) / 256.F) / 1024.F + guard + 1e-5F;
    return value - error > clip(p) + guard ? value : raw_prefix(p, base);
}
float live_edit_cpu_distance(math::Vec3 p, const LiveEditProgram& program, bool source_defined,
                             std::optional<float> stored_base) {
    std::array<float, 32> stack{};
    std::size_t size = program.base.program.empty() ? 0 : 1;
    if (size)
        stack[0] = source_defined ? raw_prefix(p, program.base)
                                  : stored_base.value_or(live_edit_cpu_base(p, program.base));
    for (std::size_t i = program.base.program.size(); i < program.edited.program.size(); ++i) {
        const auto& node = program.edited.program[i];
        if (node.center_code.w < 10)
            stack.at(size++) = authored_primitive_distance(p, node);
        else {
            const float b = stack.at(--size), a = stack.at(--size);
            stack.at(size++) = op(a, b, node);
        }
    }
    return size ? std::max(stack[0], clip(p)) : 1000.F;
}
} // namespace cubey::projects::sdf
