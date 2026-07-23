#pragma once

#include <cubey/procedural/artifact_metadata.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cubey::procedural {

inline constexpr std::uint32_t kProceduralArtifactCacheSchemaVersion = 1U;
inline constexpr std::uintmax_t kDefaultProceduralArtifactCacheBytes = 512ULL * 1024ULL * 1024ULL;

struct ProceduralArtifactRecipe {
    std::string name{};
    std::string generator{};
    std::string formula_version{};
    std::string domain{};
    std::uint64_t seed = 0U;
    std::uint64_t parameter_hash = 0U;
    ProceduralDomainSpace space = ProceduralDomainSpace::Local;
    ProceduralArtifactKind kind = ProceduralArtifactKind::ScalarField2D;
    ProceduralArtifactValueFormat format = ProceduralArtifactValueFormat::Unknown;
    ProceduralArtifactExtent extent{};
};

[[nodiscard]] std::uint64_t procedural_artifact_recipe_hash(const ProceduralArtifactRecipe& recipe);
[[nodiscard]] std::size_t
procedural_artifact_payload_byte_count(const ProceduralArtifactRecipe& recipe);
void validate_procedural_artifact_recipe(const ProceduralArtifactRecipe& recipe);

enum class ProceduralArtifactCacheLoadOutcome {
    Hit,
    Miss,
    Rejected,
};

struct CachedProceduralArtifact {
    ProceduralArtifactMetadata metadata{};
    std::vector<std::uint8_t> payload{};
};

struct ProceduralArtifactCacheLoadResult {
    ProceduralArtifactCacheLoadOutcome outcome = ProceduralArtifactCacheLoadOutcome::Miss;
    std::optional<CachedProceduralArtifact> artifact{};
    std::filesystem::path path{};
    std::string diagnostic{};
};

struct ProceduralArtifactCacheStoreResult {
    bool stored = false;
    std::filesystem::path path{};
    std::string diagnostic{};
};

struct ProceduralArtifactCacheConfig {
    std::filesystem::path root{};
    std::uintmax_t max_bytes = kDefaultProceduralArtifactCacheBytes;
};

class ProceduralArtifactCache {
  public:
    explicit ProceduralArtifactCache(ProceduralArtifactCacheConfig config);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path entry_path(const ProceduralArtifactRecipe& recipe) const;
    [[nodiscard]] ProceduralArtifactCacheLoadResult load(const ProceduralArtifactRecipe& recipe);
    [[nodiscard]] ProceduralArtifactCacheStoreResult
    store(const ProceduralArtifactRecipe& recipe, const ProceduralArtifactMetadata& metadata,
          std::span<const std::uint8_t> payload);
    void prune();

  private:
    ProceduralArtifactCacheConfig config_{};
};

[[nodiscard]] std::filesystem::path default_procedural_artifact_cache_root();

} // namespace cubey::procedural
