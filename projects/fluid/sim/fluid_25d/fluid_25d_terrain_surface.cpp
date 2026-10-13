#include "fluid_25d_terrain_surface.h"

#include <cubey/asset/file_digest.h>
#include <cubey/asset/terrain_raster_climate_source.h>
#include <cubey/asset/terrain_raster_height_source.h>
#include <cubey/terrain/terrain_surface_field.h>
#include <cubey/terrain/terrain_surface_model.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {
namespace {
// Separable area averages, truncated and renormalized at patch edges. O(cells),
// not an erosion pass. All distances are physical metres, not render scale.
std::vector<float> average(std::span<const float> input, std::uint32_t width, std::uint32_t height,
                           unsigned radius) {
    std::vector<float> horizontal(input.size()), result(input.size());
    for (unsigned z = 0; z < height; ++z) {
        double sum = 0.0;
        unsigned lo = 0, hi = 0;
        for (unsigned x = 0; x < width; ++x) {
            const unsigned end = std::min(width, x + radius + 1U);
            const unsigned begin = x > radius ? x - radius : 0U;
            while (hi < end)
                sum += input[std::size_t(z) * width + hi++];
            while (lo < begin)
                sum -= input[std::size_t(z) * width + lo++];
            horizontal[std::size_t(z) * width + x] = float(sum / (hi - lo));
        }
    }
    for (unsigned x = 0; x < width; ++x) {
        double sum = 0.0;
        unsigned lo = 0, hi = 0;
        for (unsigned z = 0; z < height; ++z) {
            const unsigned end = std::min(height, z + radius + 1U);
            const unsigned begin = z > radius ? z - radius : 0U;
            while (hi < end)
                sum += horizontal[std::size_t(hi++) * width + x];
            while (lo < begin)
                sum -= horizontal[std::size_t(lo++) * width + x];
            result[std::size_t(z) * width + x] = float(sum / (hi - lo));
        }
    }
    return result;
}
math::Vec4 encode(const terrain::TerrainSurfaceWeights& w) {
    return {w.rock, w.vegetation, w.moisture, w.snow};
}
} // namespace

unsigned fluid_25d_terrain_surface_mode(std::string_view mode) {
    if (mode == "legacy")
        return 0U;
    if (mode == "landform")
        return 1U;
    if (mode == "correlated")
        return 3U;
    if (mode == "climate")
        return 2U;
    throw std::runtime_error("unknown Scenic terrain surface mode");
}

Fluid25DTerrainSurface
fluid_25d_build_terrain_surface(std::uint32_t width, std::uint32_t height, float spacing_m,
                                std::span<const float> bed,
                                std::span<const math::Vec4> climate_samples) {
    const std::size_t count = std::size_t(width) * height;
    if (width < 2U || height < 2U || count > 4'194'304U || bed.size() != count ||
        climate_samples.size() != count || !std::isfinite(spacing_m) || spacing_m < 1.0F ||
        !std::all_of(bed.begin(), bed.end(), [](float h) { return std::isfinite(h); }))
        throw std::runtime_error("invalid Scenic terrain surface grid");
    const auto radius = [=](float metres) {
        return std::min(std::max(width, height), std::max(1U, unsigned(metres / spacing_m)));
    };
    const auto fine = average(bed, width, height, radius(90.0F));
    const auto local = average(bed, width, height, radius(360.0F));
    const auto broad = average(bed, width, height, radius(1440.0F));
    const auto [low, high] = std::minmax_element(bed.begin(), bed.end());
    const float relief = std::max(*high - *low, 1.0F);
    Fluid25DTerrainSurface result;
    result.width = width;
    result.height = height;
    result.landform.reserve(count);
    result.climate.reserve(count);
    math::Vec4 climate_sum{0.0F}, landform_sum{0.0F};
    for (unsigned z = 0; z < height; ++z) {
        for (unsigned x = 0; x < width; ++x) {
            const auto at = [&](unsigned px, unsigned pz) {
                return fine[std::size_t(pz) * width + px];
            };
            const unsigned x0 = x == 0U ? 0U : x - 1U, x1 = std::min(x + 1U, width - 1U);
            const unsigned z0 = z == 0U ? 0U : z - 1U, z1 = std::min(z + 1U, height - 1U);
            const float gx = (at(x1, z) - at(x0, z)) / (float(x1 - x0) * spacing_m);
            const float gz = (at(x, z1) - at(x, z0)) / (float(z1 - z0) * spacing_m);
            const float normal_y = 1.0F / std::sqrt(1.0F + gx * gx + gz * gz);
            const std::size_t i = std::size_t(z) * width + x;
            const auto c = climate_samples[i];
            terrain::TerrainSurfaceInputs inputs{
                .normalized_height = (bed[i] - *low) / relief,
                .slope = 1.0F - normal_y,
                .normal_y = normal_y,
                .concavity_m = 0.35F * (local[i] - fine[i]) + 0.65F * (broad[i] - fine[i]),
                .relief_scale_m = relief,
                .climate = terrain::TerrainClimateSample{c.x, c.y, c.z, c.w}};
            const auto a = encode(terrain::terrain_surface_weights(
                terrain::TerrainSurfaceModel::LandformTransition, inputs));
            const auto b = encode(terrain::terrain_surface_weights(
                terrain::TerrainSurfaceModel::ClimateTransition, inputs));
            result.landform.push_back(a);
            result.climate.push_back(b);
            landform_sum += a;
            climate_sum += b;
        }
    }
    const auto means = [=](math::Vec4 sum) {
        return nlohmann::json::array({sum.x / float(count), sum.y / float(count),
                                      sum.z / float(count), sum.w / float(count)});
    };
    result.receipt = nlohmann::json{
        {"schema", "cubey.fluid25d.terrain-surface.v1"},
        {"formula", terrain::kTerrainSurfaceModelFormulaVersion},
        {"channels", {"rock", "cover_potential", "moisture_potential", "snow_potential"}},
        {"smoothing_radii_m", {90, 360, 1440}},
        {"grid", {width, height, spacing_m}},
        {"bed_sha256", asset::sha256_hex(std::as_bytes(bed))},
        {"landform_sha256", asset::sha256_hex(std::as_bytes(std::span(result.landform)))},
        {"climate_weights_sha256", asset::sha256_hex(std::as_bytes(std::span(result.climate)))},
        {"landform_mean", means(landform_sum)},
        {"climate_mean", means(climate_sum)},
        {"climate_is_live_wetness", false},
        {"height_modified",
         false}}.dump();
    return result;
}

Fluid25DTerrainSurface fluid_25d_load_terrain_surface(const std::filesystem::path& heightfield,
                                                      const nlohmann::json& identity,
                                                      std::uint32_t width, std::uint32_t height,
                                                      float spacing_m, std::span<const float> bed,
                                                      std::span<const float> source_bed) {
    asset::TerrainRasterHeightSource source(heightfield);
    asset::TerrainRasterClimateSource climate(heightfield.parent_path() / "surface-fields.json");
    asset::validate_terrain_climate_binding(source, climate);
    if (std::filesystem::file_size(heightfield) > 65536U)
        throw std::runtime_error("oversized Scenic surface source manifest");
    std::ifstream manifest_stream(heightfield, std::ios::binary);
    const std::string manifest_text{std::istreambuf_iterator<char>(manifest_stream), {}};
    const auto manifest_hash = asset::sha256_hex(std::as_bytes(std::span(manifest_text)));
    const auto crop = identity.at("crop_xzwh").get<std::array<unsigned, 4>>();
    if (source.provenance().elevation_sha256 !=
            identity.at("elevation_sha256").get<std::string>() ||
        manifest_hash != identity.at("manifest_sha256").get<std::string>() ||
        source.sample_spacing_m() != spacing_m || identity.at("crop_cell_size_m") != spacing_m ||
        crop[2] != width || crop[3] != height || source_bed.size() != std::size_t(width) * height ||
        std::uint64_t(crop[0]) + width > source.width() ||
        std::uint64_t(crop[1]) + height > source.height() ||
        asset::sha256_hex(std::as_bytes(source_bed)) !=
            identity.at("transformed_crop_sha256").get<std::string>())
        throw std::runtime_error("Scenic surface source does not match recorded terrain identity");
    std::vector<math::Vec4> samples;
    samples.reserve(source_bed.size());
    const auto origin = source.bounds().minimum_xz;
    for (unsigned z = 0; z < height; ++z) {
        for (unsigned x = 0; x < width; ++x) {
            const math::Vec2 point =
                origin + math::Vec2(float(crop[0] + x), float(crop[1] + z)) * spacing_m;
            if (!climate.contains(point) ||
                source.sample_height({.world_xz = point}) != source_bed[std::size_t(z) * width + x])
                throw std::runtime_error("Scenic climate/source crop orientation mismatch");
            const auto c = climate.sample(point);
            samples.emplace_back(c.temperature_mean_c, c.temperature_stddev_c,
                                 c.precipitation_annual_mm, c.precipitation_cv);
        }
    }
    auto result = fluid_25d_build_terrain_surface(width, height, spacing_m, bed, samples);
    const auto preparation_started = std::chrono::steady_clock::now();
    result.source_center_xz_m = origin + (math::Vec2(float(crop[0]), float(crop[1])) +
                                          math::Vec2(float(width - 1), float(height - 1)) * 0.5F) *
                                             spacing_m;
    const auto field = terrain::make_terrain_surface_field(source, source.bounds(), &climate);
    result.correlated.reserve(samples.size());
    for (unsigned z = 0; z < height; ++z)
        for (unsigned x = 0; x < width; ++x) {
            const auto point =
                origin + math::Vec2(float(crop[0] + x), float(crop[1] + z)) * spacing_m;
            result.correlated.push_back(field.sample(point));
        }
    auto receipt = nlohmann::json::parse(result.receipt);
    receipt["source_manifest"] = std::filesystem::absolute(heightfield).string();
    receipt["source_identity"] = identity;
    receipt["material_world_center_xz_m"] = {result.source_center_xz_m.x,
                                             result.source_center_xz_m.y};
    receipt["climate_sha256"] = climate.metadata().climate_sha256;
    receipt["climate_sample_spacing_m"] = climate.metadata().sample_spacing_m;
    receipt["correlated_formula"] = terrain::kTerrainSurfaceFieldFormula;
    receipt["correlated_source_grid"] = {field.width, field.height};
    const auto field_spacing = (field.bounds.maximum_xz - field.bounds.minimum_xz) /
                               math::Vec2(float(field.width - 1), float(field.height - 1));
    receipt["correlated_source_spacing_m"] = {field_spacing.x, field_spacing.y};
    receipt["correlated_effective_filter_radii_m"] = nlohmann::json::array();
    for (float radius : {90.0F, 360.0F, 1440.0F})
        receipt["correlated_effective_filter_radii_m"].push_back(
            {std::min(float(field.width), std::floor(radius / field_spacing.x)) * field_spacing.x,
             std::min(float(field.height), std::floor(radius / field_spacing.y)) *
                 field_spacing.y});
    receipt["correlated_preparation_ms"] =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  preparation_started)
            .count();
    receipt["correlated_retained_crop_cpu_bytes"] = result.correlated.size() * sizeof(math::Vec4);
    receipt["correlated_transient_source_weights_bytes"] =
        field.weights.size() * sizeof(math::Vec4);
    receipt["correlated_crop_independent"] = true;
    receipt["correlated_sha256"] = asset::sha256_hex(std::as_bytes(std::span(result.correlated)));
    result.receipt = receipt.dump();
    return result;
}
Fluid25DTerrainSurface
fluid_25d_load_sampled_terrain_surface(const std::filesystem::path& binding_manifest,
                                       std::uint32_t width, std::uint32_t height, float spacing_m,
                                       std::span<const float> bed) {
    if (width < 2 || height < 2 || std::size_t(width) * height > 4194304U ||
        !std::isfinite(spacing_m) || spacing_m < 1.0F)
        throw std::runtime_error("invalid sampled surface grid geometry");
    if (std::filesystem::is_symlink(binding_manifest) ||
        !std::filesystem::is_regular_file(binding_manifest) ||
        std::filesystem::file_size(binding_manifest) > 65536U)
        throw std::runtime_error("invalid sampled terrain binding manifest");
    std::ifstream input(binding_manifest);
    nlohmann::json id;
    input >> id;
    const auto& grid = id.at("target_grid");
    if (id.at("schema") != "cubey.terrain.sampled-source.v1" ||
        grid.at("schema") != "cubey.terrain.sampled-grid.v1" ||
        grid.at("orientation") != "row-major-x-fastest-z-rows" ||
        grid.at("sampling") != "bilinear-lod0" || grid.at("width") != width ||
        grid.at("height") != height || grid.at("spacing_m") != spacing_m ||
        bed.size() != std::size_t(width) * height ||
        asset::sha256_hex(std::as_bytes(bed)) != id.at("solver_bed_sha256").get<std::string>())
        throw std::runtime_error("sampled terrain/native grid or bed identity mismatch");
    const auto origin = grid.at("sample_origin_xz_m").get<std::array<float, 2>>();
    if (!std::isfinite(origin[0]) || !std::isfinite(origin[1]))
        throw std::runtime_error("nonfinite sampled terrain origin");
    const std::filesystem::path heightfield = id.at("manifest_path").get<std::string>();
    if (std::filesystem::is_symlink(heightfield) ||
        !std::filesystem::is_regular_file(heightfield) ||
        std::filesystem::file_size(heightfield) > 65536U)
        throw std::runtime_error("invalid sampled terrain source path");
    std::ifstream source_stream(heightfield, std::ios::binary);
    const std::string source_text{std::istreambuf_iterator<char>(source_stream), {}};
    if (asset::sha256_hex(std::as_bytes(std::span(source_text))) !=
        id.at("manifest_sha256").get<std::string>())
        throw std::runtime_error("sampled terrain source manifest changed");
    asset::TerrainRasterHeightSource source(heightfield);
    asset::TerrainRasterClimateSource climate(heightfield.parent_path() / "surface-fields.json");
    asset::validate_terrain_climate_binding(source, climate);
    if (source.provenance().elevation_sha256 != id.at("elevation_sha256").get<std::string>())
        throw std::runtime_error("sampled terrain source elevation changed");
    std::optional<asset::TerrainSite> site;
    if (id.contains("terrain_site")) {
        const auto& binding = id.at("terrain_site");
        const std::filesystem::path path = binding.at("manifest_path").get<std::string>();
        if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path) ||
            std::filesystem::file_size(path) > 65536)
            throw std::runtime_error("invalid prepared terrain site path");
        std::ifstream stream(path, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
        if (asset::sha256_hex(std::as_bytes(std::span(bytes))) !=
            binding.at("manifest_sha256").get<std::string>())
            throw std::runtime_error("prepared terrain site changed");
        site = asset::load_terrain_site(path);
        const auto center = math::Vec2{origin[0], origin[1]} +
                            math::Vec2{float(width - 1), float(height - 1)} * spacing_m * .5F;
        if (site->world_id != binding.at("world_id").get<std::string>() ||
            site->heightfield != std::filesystem::canonical(heightfield) ||
            glm::any(glm::greaterThan(glm::abs(site->center_xz_m - center), math::Vec2{.001F})))
            throw std::runtime_error("prepared terrain site/source/crop mismatch");
    }
    const auto relative = std::filesystem::path(id.at("source_bed_path").get<std::string>());
    if (relative.has_parent_path() || relative.empty() || relative.is_absolute())
        throw std::runtime_error("unsafe sampled source-bed path");
    const auto path = binding_manifest.parent_path() / relative;
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path) != bed.size_bytes())
        throw std::runtime_error("sampled source-bed size mismatch");
    std::vector<float> reference(bed.size());
    std::ifstream payload(path, std::ios::binary);
    payload.read(reinterpret_cast<char*>(reference.data()), std::streamsize(bed.size_bytes()));
    if (!payload || asset::sha256_hex(std::as_bytes(std::span(reference))) !=
                        id.at("source_bed_sha256").get<std::string>())
        throw std::runtime_error("sampled source-bed integrity mismatch");
    const auto point = [&](unsigned x, unsigned z) {
        return math::Vec2(origin[0], origin[1]) + math::Vec2(float(x), float(z)) * spacing_m;
    };
    std::vector<math::Vec4> samples;
    samples.reserve(bed.size());
    float maximum_error = 0.0F;
    for (unsigned z = 0; z < height; ++z)
        for (unsigned x = 0; x < width; ++x) {
            const auto p = point(x, z);
            if (!source.contains_disk(p, 0) || !climate.contains(p))
                throw std::runtime_error("sampled terrain/climate coverage mismatch");
            const auto expected = source.sample_height({.world_xz = p});
            const auto supplied = reference[std::size_t(z) * width + x];
            const float error = std::abs(expected - supplied);
            const float tolerance = std::max(0.0002F, 8 * std::numeric_limits<float>::epsilon() *
                                                          std::max(std::abs(expected), 1.0F));
            if (!std::isfinite(supplied) || error > tolerance)
                throw std::runtime_error("sampled source-bed orientation/interpolation mismatch");
            maximum_error = std::max(maximum_error, error);
            const auto c = climate.sample(p);
            samples.emplace_back(c.temperature_mean_c, c.temperature_stddev_c,
                                 c.precipitation_annual_mm, c.precipitation_cv);
        }
    auto result = fluid_25d_build_terrain_surface(width, height, spacing_m, bed, samples);
    const auto started = std::chrono::steady_clock::now();
    result.source_center_xz_m =
        point(0, 0) + math::Vec2(float(width - 1), float(height - 1)) * spacing_m * 0.5F;
    const auto field = terrain::make_terrain_surface_field(source, source.bounds(), &climate);
    if (site) {
        result.site = std::move(site);
        result.context = terrain::make_terrain_surface_context(
            {{width, height, spacing_m, {origin[0], origin[1]}}, bed, {}}, source, field);
    }
    result.correlated.reserve(bed.size());
    for (unsigned z = 0; z < height; ++z)
        for (unsigned x = 0; x < width; ++x)
            result.correlated.push_back(field.sample(point(x, z)));
    auto receipt = nlohmann::json::parse(result.receipt);
    receipt["source_identity"] = id;
    receipt["material_world_center_xz_m"] = {result.source_center_xz_m.x,
                                             result.source_center_xz_m.y};
    receipt["source_sample_max_abs_error_m"] = maximum_error;
    if (result.site) {
        receipt["world_id"] = result.site->world_id;
        receipt["site_id"] = result.site->id;
        receipt["context_vertices"] = result.context->vertices.size();
        receipt["context_triangles"] = result.context->indices.size() / 3;
        receipt["context_ground_only"] = true;
        receipt["context_build_policy"] = "once-per-site-grid; no live exterior wetness";
    }
    receipt["climate_sha256"] = climate.metadata().climate_sha256;
    receipt["correlated_formula"] = terrain::kTerrainSurfaceFieldFormula;
    receipt["correlated_crop_independent"] = true;
    receipt["correlated_preparation_ms"] =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    result.receipt = receipt.dump();
    return result;
}
} // namespace cubey::projects::fluid::fluid_25d
