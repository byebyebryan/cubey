#include <algorithm>
#include <array>
#include <cmath>
#include <cubey/asset/material_recipe.h>
#include <cubey/procedural/surface_material.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
double rough_diffuse(double nl, double nv, double lv, double roughness) {
    const double sigma = roughness * std::acos(-1.0) * .5, s2 = sigma * sigma;
    return 1 - .5 * s2 / (s2 + .33) +
           .45 * s2 / (s2 + .09) * std::max(0.0, lv - nl * nv) / std::max({nl, nv, .001});
}
double sheen_lambda(double cosine, double alpha) {
    const double t = (1 - alpha) * (1 - alpha);
    auto helper = [&](double x) {
        const double a = std::lerp(21.5473, 25.3245, t), b = std::lerp(3.82987, 3.32435, t),
                     c = std::lerp(.19823, .16801, t), d = std::lerp(-1.97760, -1.27393, t),
                     e = std::lerp(-4.32054, -4.85967, t);
        return a / (1 + b * std::pow(x, c)) + d * x + e;
    };
    return std::exp(cosine < .5 ? helper(cosine) : 2 * helper(.5) - helper(1 - cosine));
}
double sheen_brdf(double nv, double nl, double nh, double roughness) {
    const double alpha = std::pow(std::max(roughness, .000001), 2), inverse = 1 / alpha;
    const double distribution =
        (2 + inverse) * std::pow(std::max(1 - nh * nh, 0.0), inverse * .5) / (2 * std::acos(-1.0));
    const double denominator =
        (1 + sheen_lambda(nv, alpha) + sheen_lambda(nl, alpha)) * 4 * nv * nl;
    return distribution * std::clamp(1 / std::max(denominator, .000001), 0.0, 1.0);
}
} // namespace

// CPU reference for GPU records decoded from the actual RT recipe SSBO.
// Inputs are the GPU's recorded float points/footprints, not reconstructed
// trigonometry; evaluation uses the separate shared CPU implementation.
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::invalid_argument(
                "usage: material_reference metrics.json probe.bin result.json");
        using namespace cubey;
        std::ifstream metadata_file(argv[1]);
        const auto metadata = nlohmann::json::parse(metadata_file);
        const auto& table = metadata.at("workshop_materials");
        std::vector<asset::MaterialRecipe> recipes;
        for (const auto& item : table)
            recipes.push_back(asset::parse_material_recipe(item.at("resolved_recipe").dump()));
        const auto width = metadata.at("extent").at(0).get<std::size_t>();
        const auto height = metadata.at("extent").at(1).get<std::size_t>();
        if (!width || !height || recipes.empty() || width * height > 131072)
            throw std::invalid_argument("invalid material reference extent or catalog");
        using Record = std::array<math::Vec4, 16>;
        static_assert(sizeof(Record) == 256);
        std::vector<Record> data(width * height);
        std::ifstream raw(argv[2], std::ios::binary);
        raw.read(reinterpret_cast<char*>(data.data()),
                 static_cast<std::streamsize>(data.size() * sizeof(Record)));
        if (!raw || raw.peek() != std::char_traits<char>::eof())
            throw std::invalid_argument("material observation byte count mismatch");
        const bool patterns = metadata.at("config").at("pattern").get<bool>();
        const bool bump = metadata.at("config").at("bump").get<bool>();
        std::vector<double> component_error(recipes.size()), normal_error(recipes.size());
        std::vector<std::size_t> counts(recipes.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            const auto slot = (i / width) % recipes.size();
            const auto& r = recipes[slot];
            const auto& actual = data[i];
            for (int channel = 0; channel < 4; ++channel)
                if (!std::isfinite(actual[0][channel]) || !std::isfinite(actual[6][channel]))
                    throw std::runtime_error("nonfinite material point or normal");
            if (actual[0].w < 0 || glm::length(math::Vec3(actual[6])) < .5F)
                throw std::runtime_error("invalid material footprint or normal length");
            const auto value = procedural::sample_surface_domain(
                r.surface, r.pattern, r.library, r.material_from_object, math::Vec3(actual[0]),
                actual[0].w, patterns);
            const float metallic = value.metallic < 0 ? r.optics.settings.x : value.metallic;
            const std::array<math::Vec4, 7> expected{
                value.material.base_roughness,
                math::Vec4(value.height_gradient, metallic),
                math::Vec4(math::Vec3(value.material.emission), value.height),
                value.sheen,
                math::Vec4(value.rough_glass, r.optics.settings.y, r.optics.settings.z,
                           r.optics.settings.w),
                r.optics.attenuation,
                math::Vec4(static_cast<float>(slot), r.optics.settings.y > 0 ? 1.F : 0.F,
                           static_cast<float>(r.version), 0)};
            const std::array<std::size_t, 7> offsets{1, 2, 3, 4, 5, 7, 8};
            for (std::size_t field = 0; field < expected.size(); ++field)
                for (int channel = 0; channel < 4; ++channel) {
                    if (!std::isfinite(actual[offsets[field]][channel]) ||
                        !std::isfinite(expected[field][channel]))
                        throw std::runtime_error("nonfinite material observation");
                    component_error[slot] = std::max(
                        component_error[slot], std::abs(double(actual[offsets[field]][channel]) -
                                                        expected[field][channel]));
                }
            const auto angles = actual[9];
            const std::array<double, 5> lobes{
                rough_diffuse(angles.y, angles.x, angles.w, r.optics.attenuation.w),
                rough_diffuse(1, 1, 1, r.optics.attenuation.w),
                value.sheen.r * sheen_brdf(angles.x, angles.y, angles.z, value.sheen.w),
                value.sheen.g * sheen_brdf(angles.x, angles.y, angles.z, value.sheen.w),
                value.sheen.b * sheen_brdf(angles.x, angles.y, angles.z, value.sheen.w)};
            for (std::size_t channel = 0; channel < lobes.size(); ++channel) {
                const double observed = channel < 2 ? actual[10][static_cast<int>(channel)]
                                                    : actual[11][static_cast<int>(channel - 2)];
                if (!std::isfinite(observed) || !std::isfinite(lobes[channel]))
                    throw std::runtime_error("nonfinite surface lobe");
                component_error[slot] =
                    std::max(component_error[slot], std::abs(observed - lobes[channel]));
            }
            const auto normal = glm::normalize(math::Vec3(.3F, .8F, .2F));
            const auto expected_normal =
                bump ? procedural::material_shading_normal(normal, value.height_gradient) : normal;
            const auto a = glm::normalize(glm::dvec3(actual[6]));
            const auto b = glm::normalize(glm::dvec3(expected_normal));
            normal_error[slot] = std::max(
                normal_error[slot], glm::degrees(std::acos(std::clamp(glm::dot(a, b), -1.0, 1.0))));
            ++counts[slot];
        }
        bool passed = true;
        auto cases = nlohmann::json::array();
        for (std::size_t slot = 0; slot < recipes.size(); ++slot) {
            passed = passed && component_error[slot] <= 5e-4 && normal_error[slot] <= .5;
            cases.push_back({{"slot", slot},
                             {"catalog_id", table[slot].at("catalog_id")},
                             {"resolved_id", recipes[slot].id},
                             {"samples", counts[slot]},
                             {"maximum_component_error", component_error[slot]},
                             {"maximum_normal_degrees", normal_error[slot]}});
        }
        const nlohmann::json result{
            {"passed", passed},
            {"samples", data.size()},
            {"component_limit", 5e-4},
            {"normal_degrees_limit", .5},
            {"cases", cases},
            {"scope",
             "shared CPU evaluator versus actual RT recipe-record sampling; not transport"}};
        std::ofstream output(argv[3]);
        if (!output)
            throw std::runtime_error("cannot write material reference receipt");
        output << result.dump(2) << '\n';
        std::cout << result.dump(2) << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
