#include "sdf_cached_stroke.h"
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cubey::projects::sdf {
CachedStrokeProgram prepare_cached_stroke(AuthoredScene base, AuthoredScene edited,
                                          bool rounded_segments) {
    using Json = nlohmann::json;
    if (Json::parse(serialize_authored_scene(base)).at("materials") !=
        Json::parse(serialize_authored_scene(edited)).at("materials"))
        throw std::invalid_argument("stroke requires an unchanged material palette");
    if (base.environment_only_gallery || edited.environment_only_gallery ||
        std::any_of(edited.optics.begin(), edited.optics.end(),
                    [](const auto& o) { return !default_optics(o); }))
        throw std::invalid_argument("stroke requires opaque authored geometry");
    const auto count = base.program.size();
    if (edited.leaf_count < base.leaf_count || edited.leaf_count - base.leaf_count > 8 ||
        edited.program.size() < count ||
        (count && std::memcmp(base.program.data(), edited.program.data(),
                              count * sizeof(SceneInstruction)) != 0))
        throw std::invalid_argument("stroke changed base prefix or exceeded eight pending leaves");
    CachedStrokeProgram result;
    result.controls.header[3] = rounded_segments ? 1U : 0U;
    result.controls.header[2] = static_cast<std::uint32_t>(count);
    const auto n = edited.leaf_count - base.leaf_count;
    if (edited.program.size() != count + (n ? 2 * n - (count == 0 ? 1 : 0) : 0))
        throw std::invalid_argument("stroke requires an ordered primitive/operator suffix");
    std::size_t at = count;
    unsigned operation = 10;
    float operator_material = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& tool = edited.program.at(at++);
        if (tool.center_code.w != 2.F &&
            (!rounded_segments || (tool.center_code.w != 3.F && tool.center_code.w != 7.F)))
            throw std::invalid_argument("stroke tool is outside the admitted primitive set");
        unsigned op = 10;
        if (count || i) {
            const auto& instruction = edited.program.at(at++);
            op = static_cast<unsigned>(instruction.center_code.w);
            if (i && instruction.settings.w != operator_material)
                throw std::invalid_argument("stroke requires fixed operator material settings");
            operator_material = instruction.settings.w;
        }
        if ((op != 10 && op != 11) || (i && op != operation))
            throw std::invalid_argument("stroke requires one hard operation until drain");
        operation = op;
        if (i &&
            ((!rounded_segments && tool.parameters.x != result.controls.tools[0].parameters.x) ||
             tool.settings.x != result.controls.tools[0].settings.x ||
             tool.settings.z != result.controls.tools[0].settings.z))
            throw std::invalid_argument(
                "stroke requires fixed scale/material and legacy sphere radius");
        result.controls.tools[i] = tool;
    }
    result.controls.header[0] = static_cast<std::uint32_t>(n);
    result.controls.header[1] = operation == 11 ? 1U : 0U;
    result.base = std::move(base);
    result.edited = std::move(edited);
    return result;
}
} // namespace cubey::projects::sdf
