#include <cubey/procedural/artifact_metadata.h>

#include <stdexcept>

namespace cubey::procedural {

void validate_procedural_artifact_extent(const ProceduralArtifactExtent& extent) {
    if (extent.width == 0U || extent.height == 0U || extent.depth == 0U ||
        extent.faces == 0U || extent.mip_levels == 0U) {
        throw std::runtime_error("procedural artifact extent dimensions must be non-zero");
    }
}

void validate_procedural_artifact_metadata(const ProceduralArtifactMetadata& metadata) {
    if (metadata.name.empty()) {
        throw std::runtime_error("procedural artifact metadata name must be non-empty");
    }
    if (metadata.generator.empty()) {
        throw std::runtime_error("procedural artifact metadata generator must be non-empty");
    }
    if (metadata.formula_version.empty()) {
        throw std::runtime_error("procedural artifact metadata formula version must be non-empty");
    }
    if (metadata.domain.empty()) {
        throw std::runtime_error("procedural artifact metadata domain must be non-empty");
    }
    if (metadata.format == ProceduralArtifactValueFormat::Unknown) {
        throw std::runtime_error("procedural artifact metadata format must be known");
    }
    validate_procedural_artifact_extent(metadata.extent);
}

std::uint32_t procedural_artifact_mip_dimension(std::uint32_t dimension,
                                                std::uint32_t mip_level) {
    if (dimension == 0U) {
        throw std::runtime_error("procedural artifact mip dimension must be non-zero");
    }
    for (std::uint32_t mip = 0; mip < mip_level; ++mip) {
        dimension = dimension > 1U ? dimension / 2U : 1U;
    }
    return dimension;
}

std::size_t procedural_artifact_mip_sample_count(const ProceduralArtifactExtent& extent,
                                                 std::uint32_t mip_level) {
    validate_procedural_artifact_extent(extent);
    if (mip_level >= extent.mip_levels) {
        throw std::runtime_error("procedural artifact mip level is out of range");
    }
    const std::uint32_t width = procedural_artifact_mip_dimension(extent.width, mip_level);
    const std::uint32_t height = procedural_artifact_mip_dimension(extent.height, mip_level);
    const std::uint32_t depth = procedural_artifact_mip_dimension(extent.depth, mip_level);
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
           static_cast<std::size_t>(depth) * static_cast<std::size_t>(extent.faces);
}

std::size_t procedural_artifact_sample_count(const ProceduralArtifactExtent& extent) {
    validate_procedural_artifact_extent(extent);
    std::size_t count = 0;
    for (std::uint32_t mip = 0; mip < extent.mip_levels; ++mip) {
        count += procedural_artifact_mip_sample_count(extent, mip);
    }
    return count;
}

} // namespace cubey::procedural
