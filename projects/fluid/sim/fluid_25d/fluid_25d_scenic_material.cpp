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
    Property{"terrain_material_blend", &Fluid25DScenicMaterial::terrain_material_blend, 0.0, 1.0},
    Property{"terrain_normal_strength", &Fluid25DScenicMaterial::terrain_normal_strength, -1.0,
             2.0},
    Property{"terrain_ambient_softening", &Fluid25DScenicMaterial::terrain_ambient_softening, 0.0,
             1.0},
    Property{"terrain_diffuse_convolution", &Fluid25DScenicMaterial::terrain_diffuse_convolution,
             0.0, 1.0},
    Property{"terrain_specular_scale", &Fluid25DScenicMaterial::terrain_specular_scale, 0.0, 1.0},
    Property{"terrain_shadow_scale", &Fluid25DScenicMaterial::terrain_shadow_scale, 0.0, 1.0},
    Property{"terrain_slope_color_scale", &Fluid25DScenicMaterial::terrain_slope_color_scale, 0.0,
             1.0},
    Property{"film_begin_m", &Fluid25DScenicMaterial::film_begin_m, 0.001, 0.2},
    Property{"film_end_m", &Fluid25DScenicMaterial::film_end_m, 0.002, 1.0},
    Property{"film_roughness", &Fluid25DScenicMaterial::film_roughness, 0.2, 0.8},
    Property{"film_ground_mix", &Fluid25DScenicMaterial::film_ground_mix, 0.0, 1.0},
    Property{"water_wet_normal", &Fluid25DScenicMaterial::water_wet_normal, 0.0, 1.0},
    Property{"water_ripple_strength", &Fluid25DScenicMaterial::water_ripple_strength, 0.0, 0.3},
    Property{"water_ripple_scale_m", &Fluid25DScenicMaterial::water_ripple_scale_m, 8.0, 128.0},
    Property{"daylight_environment", &Fluid25DScenicMaterial::daylight_environment, 0.0, 1.0},
    Property{"daylight_exposure", &Fluid25DScenicMaterial::daylight_exposure, -6.0, 4.0},
    Property{"daylight_sun_scale", &Fluid25DScenicMaterial::daylight_sun_scale, 0.0, 2.0},
};
[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error("fluid 2.5D Scenic material: " + message);
}
void validate(const Fluid25DScenicMaterial& material) {
    if (material.film_begin_m >= material.film_end_m)
        invalid("film_begin_m must be below film_end_m");
    if (material.daylight_environment != 0.0F && material.daylight_environment != 1.0F)
        invalid("daylight_environment must be 0 or 1");
    if (material.terrain_normal_strength < 0.0F && material.terrain_normal_strength != -1.0F)
        invalid("terrain_normal_strength must be -1 or nonnegative");
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
    if (profile == "macro") {
        auto result = fluid_25d_scenic_material("terrain");
        result.terrain_diffuse_convolution = 1.0F;
        result.terrain_ambient_softening = 0.0F;
        result.daylight_environment = 1.0F;
        result.daylight_exposure = 0.0F;
        result.daylight_sun_scale = 0.45F;
        result.terrain_ambient = 2.0F;
        result.terrain_direct = 1.0F;
        result.water_wet_normal = 1.0F;
        result.water_ripple_strength = 0.025F;
        result.water_ripple_scale_m = 48.0F;
        result.water_roughness = 0.14F;
        result.water_scatter_scale = 0.4F;
        result.water_clarity = 0.0F;
        result.water_extinction_scale = 4.0F;
        return result;
    }
    if (profile == "terrain") {
        auto result = fluid_25d_scenic_material("refined");
        result.terrain_material_blend = 1.0F;
        result.terrain_normal_strength = 0.12F;
        result.terrain_ambient_softening = 1.0F;
        return result;
    }
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

unsigned fluid_25d_scenic_terrain_view(std::string_view view) {
    constexpr std::array names{"shaded",      "terrain-only",    "albedo",       "weights",
                               "base-normal", "detail-normal",   "roughness",    "direct",
                               "ambient",     "constant-albedo", "no-detail",    "face-normal",
                               "no-specular", "no-shadows",      "specular-only"};
    const auto found = std::find(names.begin(), names.end(), view);
    if (found == names.end())
        invalid("unknown terrain view");
    return static_cast<unsigned>(found - names.begin());
}

unsigned fluid_25d_scenic_water_view(std::string_view view) {
    constexpr std::array names{
        "shaded",         "environment-only", "direct-only", "transmission-only",
        "no-environment", "no-direct",        "no-clarity",  "no-detail",
        "depth-bands",    "coverage",         "film-weight"};
    const auto found = std::find(names.begin(), names.end(), view);
    if (found == names.end())
        invalid("unknown water view");
    return static_cast<unsigned>(found - names.begin());
}
} // namespace cubey::projects::fluid::fluid_25d
