#include "sdf_cached_edit.h"
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cubey::projects::sdf {
CachedEditProgram prepare_cached_edit(AuthoredScene base, AuthoredScene edited) {
    const auto base_json = nlohmann::json::parse(serialize_authored_scene(base));
    const auto edited_json = nlohmann::json::parse(serialize_authored_scene(edited));
    if (base_json.at("materials") != edited_json.at("materials"))
        throw std::invalid_argument("one-edit preview requires an unchanged material palette");
    if (base.environment_only_gallery || edited.environment_only_gallery ||
        std::any_of(edited.optics.begin(), edited.optics.end(),
                    [](const auto& optical) { return !default_optics(optical); }))
        throw std::invalid_argument("one-edit preview requires opaque authored geometry");
    const auto count = base.program.size();
    if (edited.program.size() < count ||
        (count && std::memcmp(base.program.data(), edited.program.data(),
                              count * sizeof(SceneInstruction)) != 0))
        throw std::invalid_argument("one-edit preview changed the retained base prefix");
    CachedEditProgram result;
    result.controls.header[2] = static_cast<std::uint32_t>(count);
    if (edited.program.size() != count) {
        if (edited.leaf_count != base.leaf_count + 1 ||
            edited.program.size() != count + (count == 0 ? 1 : 2))
            throw std::invalid_argument("one-edit preview admits exactly one pending primitive");
        const auto& primitive = edited.program.at(count);
        const auto code = static_cast<unsigned>(primitive.center_code.w);
        if ((code != 1 && code != 2) || (code == 1 && primitive.parameters.w != 0.F))
            throw std::invalid_argument("one-edit preview admits sphere or unrounded box only");
        unsigned operation = 10;
        if (count)
            operation = static_cast<unsigned>(edited.program.back().center_code.w);
        if (operation != 10 && operation != 11)
            throw std::invalid_argument("one-edit preview admits hard union/subtraction only");
        result.controls.header[0] = 1;
        result.controls.header[1] = operation == 11 ? 1U : 0U;
        result.controls.primitive = primitive;
    } else if (edited.leaf_count != base.leaf_count) {
        throw std::invalid_argument("one-edit preview source identity differs");
    }
    result.base = std::move(base);
    result.edited = std::move(edited);
    return result;
}
} // namespace cubey::projects::sdf
