#pragma once

#include <cubey/procedural/sample_domain.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace cubey::procedural {

enum class ProceduralArtifactKind {
    ScalarField2D,
    FieldSet2D,
    Texture2D,
    TextureCube,
    Volume3D,
};

enum class ProceduralArtifactValueFormat {
    Unknown,
    Rgba8Unorm,
    Rgba32Float,
    ScalarFloat32,
    ScalarUInt8,
};

struct ProceduralArtifactExtent {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;
    std::uint32_t faces = 1;
    std::uint32_t mip_levels = 1;
};

struct ProceduralArtifactIdentity {
    std::string name{};
    std::string generator{};
    std::string formula_version{};
    std::string domain{};
    std::uint64_t seed = 0;
    ProceduralDomainSpace space = ProceduralDomainSpace::Local;
};

struct ProceduralArtifactMetadata {
    std::string name{};
    std::string generator{};
    std::string formula_version{};
    std::string domain{};
    std::uint64_t seed = 0;
    ProceduralDomainSpace space = ProceduralDomainSpace::Local;
    ProceduralArtifactKind kind = ProceduralArtifactKind::ScalarField2D;
    ProceduralArtifactValueFormat format = ProceduralArtifactValueFormat::Unknown;
    ProceduralArtifactExtent extent{};
    std::uint64_t content_hash = 0;
};

void validate_procedural_artifact_extent(const ProceduralArtifactExtent& extent);
void validate_procedural_artifact_metadata(const ProceduralArtifactMetadata& metadata);
[[nodiscard]] std::uint32_t procedural_artifact_mip_dimension(std::uint32_t dimension,
                                                             std::uint32_t mip_level);
[[nodiscard]] std::size_t procedural_artifact_mip_sample_count(
    const ProceduralArtifactExtent& extent, std::uint32_t mip_level);
[[nodiscard]] std::size_t
procedural_artifact_sample_count(const ProceduralArtifactExtent& extent);

} // namespace cubey::procedural
