#include <cubey/procedural/field_metadata.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cubey::procedural {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void append_byte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void append_u32(std::uint64_t& hash, std::uint32_t value) {
    for (std::uint32_t byte_index = 0; byte_index < 4U; ++byte_index) {
        append_byte(hash, static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xffU));
    }
}

void append_u64(std::uint64_t& hash, std::uint64_t value) {
    for (std::uint32_t byte_index = 0; byte_index < 8U; ++byte_index) {
        append_byte(hash, static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xffU));
    }
}

void append_float32(std::uint64_t& hash, float value) {
    append_u32(hash, std::bit_cast<std::uint32_t>(value));
}

void append_string(std::uint64_t& hash, std::string_view value) {
    append_u64(hash, static_cast<std::uint64_t>(value.size()));
    for (const char byte : value) {
        append_byte(hash, static_cast<std::uint8_t>(byte));
    }
}

void append_grid_desc(std::uint64_t& hash, const Grid2DDesc& desc) {
    append_u32(hash, desc.width);
    append_u32(hash, desc.height);
    append_float32(hash, desc.cell_size);
    append_float32(hash, desc.origin_x);
    append_float32(hash, desc.origin_y);
}

ProceduralArtifactExtent field_extent(const Grid2DDesc& desc) {
    return ProceduralArtifactExtent{
        .width = desc.width,
        .height = desc.height,
        .depth = 1U,
        .faces = 1U,
        .mip_levels = 1U,
    };
}

ProceduralArtifactMetadata make_field_metadata(ProceduralArtifactIdentity identity,
                                               ProceduralArtifactKind kind,
                                               const Grid2DDesc& desc,
                                               std::uint64_t content_hash) {
    ProceduralArtifactMetadata metadata{
        .name = std::move(identity.name),
        .generator = std::move(identity.generator),
        .formula_version = std::move(identity.formula_version),
        .domain = std::move(identity.domain),
        .seed = identity.seed,
        .space = identity.space,
        .kind = kind,
        .format = ProceduralArtifactValueFormat::ScalarFloat32,
        .extent = field_extent(desc),
        .content_hash = content_hash,
    };
    validate_procedural_artifact_metadata(metadata);
    return metadata;
}

} // namespace

std::uint64_t scalar_field_content_hash(const ScalarField2D& field) {
    std::uint64_t hash = kFnvOffset;
    append_string(hash, "cubey.scalar-field-2d.v1");
    append_grid_desc(hash, field.desc());
    append_u64(hash, static_cast<std::uint64_t>(field.values().size()));
    for (const float value : field.values()) {
        append_float32(hash, value);
    }
    return hash;
}

std::uint64_t field_set_content_hash(const FieldSet2D& field_set) {
    std::uint64_t hash = kFnvOffset;
    append_string(hash, "cubey.field-set-2d.v1");
    append_grid_desc(hash, field_set.desc());

    std::vector<std::string> names = field_set.field_names();
    std::sort(names.begin(), names.end());
    append_u64(hash, static_cast<std::uint64_t>(names.size()));
    for (const std::string& name : names) {
        append_string(hash, name);
        append_u64(hash, scalar_field_content_hash(field_set.field(name)));
    }
    return hash;
}

ProceduralArtifactMetadata
make_scalar_field_artifact_metadata(ProceduralArtifactIdentity identity,
                                    const ScalarField2D& field) {
    return make_field_metadata(std::move(identity), ProceduralArtifactKind::ScalarField2D,
                               field.desc(), scalar_field_content_hash(field));
}

ProceduralArtifactMetadata
make_field_set_artifact_metadata(ProceduralArtifactIdentity identity,
                                 const FieldSet2D& field_set) {
    return make_field_metadata(std::move(identity), ProceduralArtifactKind::FieldSet2D,
                               field_set.desc(), field_set_content_hash(field_set));
}

} // namespace cubey::procedural
