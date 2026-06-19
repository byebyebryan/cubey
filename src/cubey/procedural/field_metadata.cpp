#include <cubey/procedural/field_metadata.h>

#include <cubey/procedural/hash.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cubey::procedural {
namespace {

void append_grid_desc(ProceduralHashBuilder& hash, const Grid2DDesc& desc) {
    hash.append_u32(desc.width);
    hash.append_u32(desc.height);
    hash.append_float32(desc.cell_size);
    hash.append_float32(desc.origin_x);
    hash.append_float32(desc.origin_y);
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
    ProceduralHashBuilder hash;
    hash.append_string("cubey.scalar-field-2d.v1");
    append_grid_desc(hash, field.desc());
    hash.append_u64(static_cast<std::uint64_t>(field.values().size()));
    for (const float value : field.values()) {
        hash.append_float32(value);
    }
    return hash.value();
}

std::uint64_t field_set_content_hash(const FieldSet2D& field_set) {
    ProceduralHashBuilder hash;
    hash.append_string("cubey.field-set-2d.v1");
    append_grid_desc(hash, field_set.desc());

    std::vector<std::string> names = field_set.field_names();
    std::sort(names.begin(), names.end());
    hash.append_u64(static_cast<std::uint64_t>(names.size()));
    for (const std::string& name : names) {
        hash.append_string(name);
        hash.append_u64(scalar_field_content_hash(field_set.field(name)));
    }
    return hash.value();
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
