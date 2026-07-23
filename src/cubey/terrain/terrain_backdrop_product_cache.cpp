#include <cubey/terrain/terrain_backdrop_product_cache.h>

#include <cubey/procedural/hash.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace cubey::terrain {
namespace {

constexpr std::array<std::uint8_t, 8U> kPayloadMagic{'C', 'U', 'B', 'E', 'Y', 'T', 'P', '1'};
constexpr std::uint32_t kPayloadVersion = 1U;
constexpr std::uint64_t kMaximumVertices = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumIndices = 96ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kMaximumSectors = 4'096U;
constexpr std::uint32_t kMaximumStringBytes = 1U << 20U;

class ByteWriter {
  public:
    void append_u32(std::uint32_t value) {
        for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
            bytes_.push_back(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
        }
    }

    void append_u64(std::uint64_t value) {
        for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
            bytes_.push_back(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
        }
    }

    void append_float(float value) {
        append_u32(std::bit_cast<std::uint32_t>(value));
    }

    void append_string(std::string_view value) {
        if (value.size() > kMaximumStringBytes) {
            throw std::runtime_error("terrain product cache string is too large");
        }
        append_u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void append_bytes(std::span<const std::uint8_t> bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::vector<std::uint8_t> finish() && {
        return std::move(bytes_);
    }

  private:
    std::vector<std::uint8_t> bytes_{};
};

class ByteReader {
  public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::span<const std::uint8_t> take(std::size_t count) {
        if (count > bytes_.size() - offset_) {
            throw std::runtime_error("terrain product cache payload is truncated");
        }
        const std::span<const std::uint8_t> result = bytes_.subspan(offset_, count);
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::uint32_t read_u32() {
        const std::span<const std::uint8_t> bytes = take(4U);
        std::uint32_t value = 0U;
        for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
            value |= static_cast<std::uint32_t>(bytes[byte]) << (byte * 8U);
        }
        return value;
    }

    [[nodiscard]] std::uint64_t read_u64() {
        const std::span<const std::uint8_t> bytes = take(8U);
        std::uint64_t value = 0U;
        for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
            value |= static_cast<std::uint64_t>(bytes[byte]) << (byte * 8U);
        }
        return value;
    }

    [[nodiscard]] float read_float() {
        return std::bit_cast<float>(read_u32());
    }

    [[nodiscard]] std::string read_string() {
        const std::uint32_t count = read_u32();
        if (count > kMaximumStringBytes) {
            throw std::runtime_error("terrain product cache string is too large");
        }
        const std::span<const std::uint8_t> bytes = take(count);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

  private:
    std::span<const std::uint8_t> bytes_{};
    std::size_t offset_ = 0U;
};

void append_request(ByteWriter& writer, const TerrainBackdropProductRequest& request) {
    writer.append_float(request.source_focus_xz.x);
    writer.append_float(request.source_focus_xz.y);
    writer.append_u32(static_cast<std::uint32_t>(request.density));
    writer.append_u32(static_cast<std::uint32_t>(request.center_mode));
    writer.append_u32(static_cast<std::uint32_t>(request.center_sampling));
    writer.append_u32(request.render_stride);
    writer.append_float(request.consumer_radius_m);
    writer.append_float(request.visible_inner_radius_m);
    writer.append_float(request.outer_radius_m);
    writer.append_float(request.vertical_scale);
    writer.append_float(request.vertical_offset_m);
}

[[nodiscard]] TerrainBackdropProductRequest read_request(ByteReader& reader) {
    TerrainBackdropProductRequest request;
    request.source_focus_xz = {reader.read_float(), reader.read_float()};
    request.density = static_cast<TerrainBackdropMeshDensity>(reader.read_u32());
    request.center_mode = static_cast<TerrainBackdropCenterMode>(reader.read_u32());
    request.center_sampling = static_cast<TerrainBackdropCenterSampling>(reader.read_u32());
    request.render_stride = reader.read_u32();
    request.consumer_radius_m = reader.read_float();
    request.visible_inner_radius_m = reader.read_float();
    request.outer_radius_m = reader.read_float();
    request.vertical_scale = reader.read_float();
    request.vertical_offset_m = reader.read_float();
    return request;
}

void append_source(ByteWriter& writer, const TerrainBackdropSourceInfo& source) {
    writer.append_string(source.id);
    writer.append_u64(source.seed);
    writer.append_float(source.base_height_m);
    writer.append_float(source.relief_scale_m);
    writer.append_float(source.gradient_step_m);
}

[[nodiscard]] TerrainBackdropSourceInfo read_source(ByteReader& reader) {
    return {
        .id = reader.read_string(),
        .seed = reader.read_u64(),
        .base_height_m = reader.read_float(),
        .relief_scale_m = reader.read_float(),
        .gradient_step_m = reader.read_float(),
    };
}

void append_diagnostics(ByteWriter& writer, const TerrainBackdropProductDiagnostics& diagnostics) {
    writer.append_u32(diagnostics.density.angular_intervals);
    writer.append_u32(diagnostics.density.center_radial_intervals);
    writer.append_u32(diagnostics.density.hidden_radial_intervals);
    writer.append_u32(diagnostics.density.visible_radial_intervals);
    writer.append_u32(diagnostics.density.sector_count);
    writer.append_u64(diagnostics.source_sample_count);
    writer.append_u64(diagnostics.sampled_vertex_count);
    writer.append_u64(diagnostics.full_triangle_count);
    writer.append_u64(diagnostics.render_vertex_count);
    writer.append_u64(diagnostics.render_triangle_count);
    writer.append_u64(diagnostics.center_sampled_vertex_count);
    writer.append_u64(diagnostics.center_full_triangle_count);
    writer.append_u64(diagnostics.center_render_vertex_count);
    writer.append_u64(diagnostics.center_render_triangle_count);
    writer.append_float(diagnostics.minimum_height_m);
    writer.append_float(diagnostics.maximum_height_m);
    writer.append_float(diagnostics.maximum_sector_boundary_delta_m);
    writer.append_u64(diagnostics.content_hash);
    writer.append_u64(diagnostics.geometry_hash);
    writer.append_float(diagnostics.mean_rock);
    writer.append_float(diagnostics.mean_snow);
    writer.append_float(diagnostics.mean_vegetation);
    writer.append_float(diagnostics.mean_moisture);
}

void append_vec3(ByteWriter& writer, cubey::math::Vec3 value) {
    writer.append_float(value.x);
    writer.append_float(value.y);
    writer.append_float(value.z);
}

[[nodiscard]] cubey::math::Vec3 read_vec3(ByteReader& reader) {
    return {reader.read_float(), reader.read_float(), reader.read_float()};
}

[[nodiscard]] TerrainBackdropProductDiagnostics read_diagnostics(ByteReader& reader) {
    TerrainBackdropProductDiagnostics diagnostics;
    diagnostics.density = {
        .angular_intervals = reader.read_u32(),
        .center_radial_intervals = reader.read_u32(),
        .hidden_radial_intervals = reader.read_u32(),
        .visible_radial_intervals = reader.read_u32(),
        .sector_count = reader.read_u32(),
    };
    diagnostics.source_sample_count = reader.read_u64();
    diagnostics.sampled_vertex_count = reader.read_u64();
    diagnostics.full_triangle_count = reader.read_u64();
    diagnostics.render_vertex_count = reader.read_u64();
    diagnostics.render_triangle_count = reader.read_u64();
    diagnostics.center_sampled_vertex_count = reader.read_u64();
    diagnostics.center_full_triangle_count = reader.read_u64();
    diagnostics.center_render_vertex_count = reader.read_u64();
    diagnostics.center_render_triangle_count = reader.read_u64();
    diagnostics.minimum_height_m = reader.read_float();
    diagnostics.maximum_height_m = reader.read_float();
    diagnostics.maximum_sector_boundary_delta_m = reader.read_float();
    diagnostics.content_hash = reader.read_u64();
    diagnostics.geometry_hash = reader.read_u64();
    diagnostics.mean_rock = reader.read_float();
    diagnostics.mean_snow = reader.read_float();
    diagnostics.mean_vegetation = reader.read_float();
    diagnostics.mean_moisture = reader.read_float();
    return diagnostics;
}

void append_mesh(ByteWriter& writer, const TerrainBackdropSectorMesh& mesh) {
    writer.append_u64(mesh.vertices.size());
    writer.append_u64(mesh.indices.size());
    append_vec3(writer, mesh.bounds.minimum);
    append_vec3(writer, mesh.bounds.maximum);
    append_vec3(writer, mesh.bounds.center);
    writer.append_float(mesh.begin_azimuth_radians);
    writer.append_float(mesh.end_azimuth_radians);
    for (const TerrainBackdropVertex& vertex : mesh.vertices) {
        for (const float value : vertex.position) {
            writer.append_float(value);
        }
        for (const float value : vertex.material) {
            writer.append_float(value);
        }
        for (const float value : vertex.normal) {
            writer.append_float(value);
        }
        for (const float value : vertex.surface) {
            writer.append_float(value);
        }
    }
    for (const std::uint32_t index : mesh.indices) {
        writer.append_u32(index);
    }
}

[[nodiscard]] TerrainBackdropSectorMesh read_mesh(ByteReader& reader) {
    const std::uint64_t vertex_count = reader.read_u64();
    const std::uint64_t index_count = reader.read_u64();
    constexpr std::size_t kVertexBytes = 11U * sizeof(float);
    if (vertex_count == 0U || vertex_count > kMaximumVertices || index_count == 0U ||
        index_count > kMaximumIndices || index_count % 3U != 0U ||
        vertex_count > reader.remaining() / kVertexBytes) {
        throw std::runtime_error("terrain product cache mesh counts are invalid");
    }
    TerrainBackdropSectorMesh mesh;
    mesh.bounds.minimum = read_vec3(reader);
    mesh.bounds.maximum = read_vec3(reader);
    mesh.bounds.center = read_vec3(reader);
    mesh.begin_azimuth_radians = reader.read_float();
    mesh.end_azimuth_radians = reader.read_float();
    mesh.vertices.resize(static_cast<std::size_t>(vertex_count));
    for (TerrainBackdropVertex& vertex : mesh.vertices) {
        for (float& value : vertex.position) {
            value = reader.read_float();
        }
        for (float& value : vertex.material) {
            value = reader.read_float();
        }
        for (float& value : vertex.normal) {
            value = reader.read_float();
        }
        for (float& value : vertex.surface) {
            value = reader.read_float();
        }
    }
    if (index_count > reader.remaining() / sizeof(std::uint32_t)) {
        throw std::runtime_error("terrain product cache indices are truncated");
    }
    mesh.indices.resize(static_cast<std::size_t>(index_count));
    for (std::uint32_t& index : mesh.indices) {
        index = reader.read_u32();
        if (index >= mesh.vertices.size()) {
            throw std::runtime_error("terrain product cache index is outside its mesh");
        }
    }
    return mesh;
}

[[nodiscard]] bool finite(float value) {
    return std::isfinite(value);
}

void validate_mesh(const TerrainBackdropSectorMesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3U != 0U ||
        !finite(mesh.begin_azimuth_radians) || !finite(mesh.end_azimuth_radians)) {
        throw std::runtime_error("terrain product cache mesh is incomplete");
    }
    const auto finite_array = [](const auto& values) {
        return std::ranges::all_of(values, [](float value) { return finite(value); });
    };
    const auto finite_vec3 = [](cubey::math::Vec3 value) {
        return finite(value.x) && finite(value.y) && finite(value.z);
    };
    if (!finite_vec3(mesh.bounds.minimum) || !finite_vec3(mesh.bounds.maximum) ||
        !finite_vec3(mesh.bounds.center)) {
        throw std::runtime_error("terrain product cache mesh bounds are invalid");
    }
    for (const TerrainBackdropVertex& vertex : mesh.vertices) {
        if (!finite_array(vertex.position) || !finite_array(vertex.material) ||
            !finite_array(vertex.normal) || !finite_array(vertex.surface)) {
            throw std::runtime_error("terrain product cache vertex is invalid");
        }
    }
}

void validate_product(const TerrainBackdropProduct& product) {
    const TerrainBackdropProductRequest& request = product.request;
    const bool valid_density = request.density == TerrainBackdropMeshDensity::Low ||
                               request.density == TerrainBackdropMeshDensity::Medium ||
                               request.density == TerrainBackdropMeshDensity::High;
    const bool valid_center_mode = request.center_mode == TerrainBackdropCenterMode::Cutout ||
                                   request.center_mode == TerrainBackdropCenterMode::Continuous;
    const bool valid_center_sampling =
        request.center_sampling == TerrainBackdropCenterSampling::SplitLinearLog ||
        request.center_sampling == TerrainBackdropCenterSampling::Uniform ||
        request.center_sampling == TerrainBackdropCenterSampling::SeamMatched;
    if (!valid_density || !valid_center_mode || !valid_center_sampling ||
        !finite(request.source_focus_xz.x) || !finite(request.source_focus_xz.y) ||
        !finite(request.consumer_radius_m) || !finite(request.visible_inner_radius_m) ||
        !finite(request.outer_radius_m) || !finite(request.vertical_scale) ||
        !finite(request.vertical_offset_m) || request.consumer_radius_m <= 0.0F ||
        request.visible_inner_radius_m <= request.consumer_radius_m ||
        request.outer_radius_m <= request.visible_inner_radius_m ||
        request.vertical_scale <= 0.0F) {
        throw std::runtime_error("terrain product cache request is invalid");
    }
    const TerrainBackdropDensityProfile expected =
        terrain_backdrop_density_profile(request.density);
    const TerrainBackdropProductDiagnostics& diagnostics = product.diagnostics;
    if (product.source.id.empty() || !finite(product.source.base_height_m) ||
        !finite(product.source.relief_scale_m) || product.source.relief_scale_m <= 0.0F ||
        !finite(product.source.gradient_step_m) || product.source.gradient_step_m <= 0.0F ||
        request.render_stride > expected.angular_intervals ||
        request.render_stride > expected.visible_radial_intervals ||
        diagnostics.density.angular_intervals != expected.angular_intervals ||
        diagnostics.density.center_radial_intervals != expected.center_radial_intervals ||
        diagnostics.density.hidden_radial_intervals != expected.hidden_radial_intervals ||
        diagnostics.density.visible_radial_intervals != expected.visible_radial_intervals ||
        diagnostics.density.sector_count != expected.sector_count ||
        product.sectors.size() != expected.sector_count ||
        product.center.has_value() !=
            (request.center_mode == TerrainBackdropCenterMode::Continuous)) {
        throw std::runtime_error("terrain product cache product identity is invalid");
    }

    std::uint64_t render_vertices = 0U;
    std::uint64_t render_triangles = 0U;
    if (product.center.has_value()) {
        validate_mesh(product.center.value());
        render_vertices += product.center->vertices.size();
        render_triangles += product.center->triangle_count();
        if (diagnostics.center_render_vertex_count != product.center->vertices.size() ||
            diagnostics.center_render_triangle_count != product.center->triangle_count()) {
            throw std::runtime_error("terrain product cache center diagnostics are invalid");
        }
    }
    for (const TerrainBackdropSectorMesh& sector : product.sectors) {
        validate_mesh(sector);
        render_vertices += sector.vertices.size();
        render_triangles += sector.triangle_count();
    }
    if (diagnostics.render_vertex_count != render_vertices ||
        diagnostics.render_triangle_count != render_triangles ||
        !finite(diagnostics.minimum_height_m) || !finite(diagnostics.maximum_height_m) ||
        diagnostics.minimum_height_m > diagnostics.maximum_height_m ||
        !finite(diagnostics.maximum_sector_boundary_delta_m) ||
        diagnostics.maximum_sector_boundary_delta_m < 0.0F || !finite(diagnostics.mean_rock) ||
        !finite(diagnostics.mean_snow) || !finite(diagnostics.mean_vegetation) ||
        !finite(diagnostics.mean_moisture)) {
        throw std::runtime_error("terrain product cache diagnostics are invalid");
    }
}

} // namespace

cubey::procedural::ProceduralArtifactRecipe
terrain_backdrop_product_cache_recipe(const TerrainBackdropProductRequest& request,
                                      const TerrainBackdropSourceInfo& source,
                                      const TerrainBackdropProductRecipeContext& context) {
    if (source.id.empty() || context.source_content_sha256.empty() ||
        context.surface_formula_version.empty()) {
        throw std::runtime_error("terrain product cache recipe context is incomplete");
    }
    cubey::procedural::ProceduralHashBuilder parameters;
    parameters.append_string("terrain-backdrop-product-cache-parameters-v1");
    parameters.append_string(source.id);
    parameters.append_string(context.source_content_sha256);
    parameters.append_string(context.climate_content_sha256);
    parameters.append_string(context.surface_formula_version);
    parameters.append_u64(context.surface_parameter_hash);
    parameters.append_u64(context.placement_parameter_hash);
    parameters.append_float32(request.source_focus_xz.x);
    parameters.append_float32(request.source_focus_xz.y);
    parameters.append_u32(static_cast<std::uint32_t>(request.density));
    parameters.append_u32(static_cast<std::uint32_t>(request.center_mode));
    parameters.append_u32(static_cast<std::uint32_t>(request.center_sampling));
    parameters.append_u32(request.render_stride);
    parameters.append_float32(request.consumer_radius_m);
    parameters.append_float32(request.visible_inner_radius_m);
    parameters.append_float32(request.outer_radius_m);
    parameters.append_float32(request.vertical_scale);
    parameters.append_float32(request.vertical_offset_m);
    parameters.append_float32(source.base_height_m);
    parameters.append_float32(source.relief_scale_m);
    parameters.append_float32(source.gradient_step_m);

    const TerrainBackdropDensityProfile density = terrain_backdrop_density_profile(request.density);
    const std::uint32_t radial_samples = density.center_radial_intervals +
                                         density.hidden_radial_intervals +
                                         density.visible_radial_intervals + 1U;
    return {
        .name = "terrain backdrop product",
        .generator = "cubey::terrain::make_terrain_backdrop_product",
        .formula_version = std::string{kTerrainBackdropProductCacheFormulaVersion},
        .domain = "terrain.backdrop.product",
        .seed = source.seed,
        .parameter_hash = parameters.value(),
        .space = cubey::procedural::ProceduralDomainSpace::Local,
        .kind = cubey::procedural::ProceduralArtifactKind::StructuredProduct,
        .format = cubey::procedural::ProceduralArtifactValueFormat::OpaqueBytes,
        .extent =
            {
                .width = density.angular_intervals,
                .height = radial_samples,
                .depth = density.sector_count,
                .faces = 1U,
                .mip_levels = 1U,
            },
    };
}

std::vector<std::uint8_t> encode_terrain_backdrop_product(const TerrainBackdropProduct& product,
                                                          std::span<const std::uint8_t> auxiliary) {
    validate_product(product);
    ByteWriter writer;
    writer.append_bytes(kPayloadMagic);
    writer.append_u32(kPayloadVersion);
    append_request(writer, product.request);
    append_source(writer, product.source);
    append_diagnostics(writer, product.diagnostics);
    writer.append_u32(product.center.has_value() ? 1U : 0U);
    writer.append_u32(static_cast<std::uint32_t>(product.sectors.size()));
    if (product.center.has_value()) {
        append_mesh(writer, product.center.value());
    }
    for (const TerrainBackdropSectorMesh& sector : product.sectors) {
        append_mesh(writer, sector);
    }
    writer.append_u64(auxiliary.size());
    writer.append_bytes(auxiliary);
    return std::move(writer).finish();
}

DecodedTerrainBackdropProduct
decode_terrain_backdrop_product(std::span<const std::uint8_t> payload) {
    ByteReader reader(payload);
    if (!std::ranges::equal(reader.take(kPayloadMagic.size()), kPayloadMagic) ||
        reader.read_u32() != kPayloadVersion) {
        throw std::runtime_error("terrain product cache payload header is incompatible");
    }
    DecodedTerrainBackdropProduct decoded;
    decoded.product.request = read_request(reader);
    decoded.product.source = read_source(reader);
    decoded.product.diagnostics = read_diagnostics(reader);
    const std::uint32_t has_center = reader.read_u32();
    const std::uint32_t sector_count = reader.read_u32();
    if (has_center > 1U || sector_count == 0U || sector_count > kMaximumSectors) {
        throw std::runtime_error("terrain product cache mesh set is invalid");
    }
    if (has_center != 0U) {
        decoded.product.center.emplace(read_mesh(reader));
    }
    decoded.product.sectors.reserve(sector_count);
    for (std::uint32_t sector = 0U; sector < sector_count; ++sector) {
        decoded.product.sectors.push_back(read_mesh(reader));
    }
    const std::uint64_t auxiliary_bytes = reader.read_u64();
    if (auxiliary_bytes > std::numeric_limits<std::size_t>::max() ||
        auxiliary_bytes > reader.remaining()) {
        throw std::runtime_error("terrain product cache auxiliary payload is truncated");
    }
    const std::span<const std::uint8_t> auxiliary =
        reader.take(static_cast<std::size_t>(auxiliary_bytes));
    decoded.auxiliary.assign(auxiliary.begin(), auxiliary.end());
    if (reader.remaining() != 0U) {
        throw std::runtime_error("terrain product cache payload has trailing bytes");
    }
    validate_product(decoded.product);
    return decoded;
}

} // namespace cubey::terrain
