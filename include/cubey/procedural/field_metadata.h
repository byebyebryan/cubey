#pragma once

#include <cubey/procedural/artifact_metadata.h>
#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/field_set_2d.h>

#include <cstdint>

namespace cubey::procedural {

[[nodiscard]] std::uint64_t scalar_field_content_hash(const ScalarField2D& field);
[[nodiscard]] std::uint64_t field_set_content_hash(const FieldSet2D& field_set);
[[nodiscard]] ProceduralArtifactMetadata
make_scalar_field_artifact_metadata(ProceduralArtifactIdentity identity,
                                    const ScalarField2D& field);
[[nodiscard]] ProceduralArtifactMetadata
make_field_set_artifact_metadata(ProceduralArtifactIdentity identity,
                                 const FieldSet2D& field_set);

} // namespace cubey::procedural
