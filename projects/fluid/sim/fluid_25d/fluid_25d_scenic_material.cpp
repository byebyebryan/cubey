#include "fluid_25d_scenic_material.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {
namespace {
constexpr std::string_view kSchema = "cubey.fluid25d.scenic-material.v2";
constexpr std::size_t kMaximumBytes = 16384U;
struct Property {
    const char* name;
    float Fluid25DScenicMaterial::* member;
    double low, high;
};
constexpr std::array kProperties{
    Property{"wet_roughness", &Fluid25DScenicMaterial::wet_roughness, 0.2, 1.0},
    Property{"wet_darkening", &Fluid25DScenicMaterial::wet_darkening, 0.5, 1.0},
    Property{"terrain_saturation", &Fluid25DScenicMaterial::terrain_saturation, 0.0, 1.3},
    Property{"terrain_ambient", &Fluid25DScenicMaterial::terrain_ambient, 0.0, 2.0},
    Property{"terrain_direct", &Fluid25DScenicMaterial::terrain_direct, 0.0, 2.0},
    Property{"shadow_strength", &Fluid25DScenicMaterial::shadow_strength, 0.0, 1.0},
    Property{"water_roughness", &Fluid25DScenicMaterial::water_roughness, 0.08, 0.5},
    Property{"water_normal_strength", &Fluid25DScenicMaterial::water_normal_strength, 0.0, 2.0},
    Property{"water_extinction_scale", &Fluid25DScenicMaterial::water_extinction_scale, 0.0, 4.0},
    Property{"water_scatter_scale", &Fluid25DScenicMaterial::water_scatter_scale, 0.0, 2.0},
    Property{"water_reflection_scale", &Fluid25DScenicMaterial::water_reflection_scale, 0.0, 2.0},
    Property{"water_scatter_lighting", &Fluid25DScenicMaterial::water_scatter_lighting, 0.0, 1.0},
    Property{"water_clarity", &Fluid25DScenicMaterial::water_clarity, 0.0, 1.0},
    Property{"terrain_mineral_scale", &Fluid25DScenicMaterial::terrain_mineral_scale, 0.4, 1.3},
};
[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error("fluid 2.5D Scenic material: " + message);
}
void validate(const Fluid25DScenicMaterial& material) {
    for (const auto& p : kProperties) {
        const float value = material.*p.member;
        if (!std::isfinite(value) || value < static_cast<float>(p.low) ||
            value > static_cast<float>(p.high))
            invalid(std::string("non-finite/out-of-range ") + p.name);
    }
}
} // namespace

Fluid25DScenicMaterial fluid_25d_scenic_material(std::string_view profile) {
    if (profile == "v1")
        return {};
    if (profile != "refined")
        invalid("unknown profile");
    return {.wet_roughness = 0.78F,
            .wet_darkening = 0.86F,
            .terrain_saturation = 0.6F,
            .terrain_ambient = 1.4F,
            .terrain_direct = 0.8F,
            .water_scatter_scale = 0.85F,
            .water_scatter_lighting = 0.7F,
            .water_clarity = 0.55F,
            .terrain_mineral_scale = 0.62F};
}

Fluid25DScenicMaterial fluid_25d_parse_scenic_material(std::string_view text,
                                                       Fluid25DScenicMaterial base) {
    if (text.empty() || text.size() > kMaximumBytes)
        invalid("empty/oversized tuning document");
    std::set<std::string> keys;
    const auto document = nlohmann::json::parse(
        text, [&](int, nlohmann::json::parse_event_t event, nlohmann::json& value) {
            if (event == nlohmann::json::parse_event_t::key &&
                !keys.insert(value.get<std::string>()).second)
                invalid("duplicate property");
            return true;
        });
    if (!document.is_object() || !document.contains("schema") || document.at("schema") != kSchema)
        invalid("wrong/missing schema");
    for (const auto& [key, value] : document.items()) {
        if (key == "schema")
            continue;
        bool found = false;
        for (const auto& p : kProperties) {
            if (key != p.name)
                continue;
            if (!value.is_number())
                invalid("numeric property required: " + key);
            const double number = value.get<double>();
            if (!std::isfinite(number) || number < p.low || number > p.high)
                invalid("non-finite/out-of-range " + key);
            base.*p.member = static_cast<float>(number);
            found = true;
            break;
        }
        if (!found)
            invalid("unknown property: " + key);
    }
    validate(base);
    return base;
}

Fluid25DScenicMaterial fluid_25d_load_scenic_material(const std::filesystem::path& path,
                                                      Fluid25DScenicMaterial base) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        invalid("missing/non-regular/symlink tuning file");
    const auto size = std::filesystem::file_size(path);
    if (size == 0U || size > kMaximumBytes)
        invalid("empty/oversized tuning file");
    std::ifstream stream(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(size));
    if (stream.gcount() != static_cast<std::streamsize>(size) || stream.peek() != EOF)
        invalid("tuning file changed or failed to read");
    return fluid_25d_parse_scenic_material(text, base);
}

std::string fluid_25d_scenic_material_json(const Fluid25DScenicMaterial& material) {
    validate(material);
    nlohmann::json document{{"schema", kSchema}};
    for (const auto& p : kProperties) {
        // Decimal range endpoints need not be exactly representable as floats.
        // Emit an in-range double so the bounded parser can read our receipt
        // back without rejecting e.g. the float representation of 0.08.
        document[p.name] = std::clamp(static_cast<double>(material.*p.member), p.low, p.high);
    }
    return document.dump();
}
} // namespace cubey::projects::fluid::fluid_25d
