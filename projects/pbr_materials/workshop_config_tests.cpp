#include "../sdf/sdf_config.h"
#include "pbr_materials_config.h"
#include <stdexcept>
#include <string>
#include <vector>
using namespace cubey::projects;
void require(bool value) {
    if (!value)
        throw std::runtime_error("workshop CLI contract failed");
}
template <class F> auto parse(F fn, std::initializer_list<const char*> values) {
    std::vector<std::string> strings;
    strings.emplace_back("workshop");
    for (auto v : values)
        strings.emplace_back(v);
    std::vector<char*> args;
    for (auto& s : strings)
        args.push_back(s.data());
    return fn(static_cast<int>(args.size()), args.data(), nullptr);
}
int main() {
    const auto mesh =
        parse(pbr_materials::parse_pbr_materials_config,
              {"--material", "marble", "--no-pattern", "--no-bump", "--lighting", "clear"});
    require(!mesh.pattern && !mesh.bump && mesh.environment == "clear");
    const auto sdf_defaults = parse(sdf::parse_sdf_config, {"--workshop", "sphere"});
    require(sdf_defaults.yaw == mesh.yaw && sdf_defaults.pitch == mesh.pitch &&
            sdf_defaults.distance == mesh.distance);
    require(sdf_defaults.exposure == mesh.pbr.exposure &&
            sdf_defaults.lighting == "material-study" && sdf_defaults.glass_crossings == 8);
    const auto explicit_values =
        parse(sdf::parse_sdf_config, {"--workshop", "csg", "--exposure", "0", "--glass-crossings",
                                      "16", "--no-workshop-blend-materials"});
    require(explicit_values.exposure == 0 && explicit_values.glass_crossings == 16 &&
            !explicit_values.workshop_blend_materials);
    const auto legacy = parse(sdf::parse_sdf_config, {});
    require(legacy.workshop.empty() && legacy.exposure == 0 && legacy.lighting == "atmosphere" &&
            legacy.glass_crossings == 4);
}
