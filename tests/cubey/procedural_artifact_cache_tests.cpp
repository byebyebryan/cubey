#include <cubey/core/file_io.h>
#include <cubey/procedural/artifact_cache.h>
#include <cubey/procedural/artifact_metadata.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class CacheFixture {
  public:
    CacheFixture() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("cubey-procedural-cache-" + std::to_string(suffix));
        std::filesystem::create_directories(root);
    }

    ~CacheFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root{};
};

[[nodiscard]] cubey::procedural::ProceduralArtifactRecipe
test_recipe(std::string formula_version = "cache-test-v1", std::uint64_t parameter_hash = 17U) {
    return {
        .name = "cache test texture",
        .generator = "cubey::tests::procedural_artifact_cache",
        .formula_version = std::move(formula_version),
        .domain = "tests.procedural.cache",
        .seed = 42U,
        .parameter_hash = parameter_hash,
        .space = cubey::procedural::ProceduralDomainSpace::Atlas,
        .kind = cubey::procedural::ProceduralArtifactKind::Texture2D,
        .format = cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
        .extent = {.width = 2U, .height = 2U, .depth = 1U, .faces = 1U, .mip_levels = 1U},
    };
}

[[nodiscard]] cubey::procedural::ProceduralArtifactMetadata
test_metadata(const cubey::procedural::ProceduralArtifactRecipe& recipe,
              std::uint64_t content_hash) {
    return cubey::procedural::make_procedural_artifact_metadata(
        cubey::procedural::make_procedural_artifact_identity(recipe.name, recipe.generator,
                                                             recipe.formula_version, recipe.domain,
                                                             recipe.seed, recipe.space),
        recipe.kind, recipe.format, recipe.extent, content_hash);
}

constexpr std::array<std::uint8_t, 16> kPayload{
    0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U,
};

} // namespace

void test_procedural_artifact_cache_hashes_complete_recipes() {
    const cubey::procedural::ProceduralArtifactRecipe recipe = test_recipe();
    const std::uint64_t hash = cubey::procedural::procedural_artifact_recipe_hash(recipe);
    require(hash == cubey::procedural::procedural_artifact_recipe_hash(recipe),
            "artifact cache recipe hashes should be stable");
    require(hash !=
                cubey::procedural::procedural_artifact_recipe_hash(test_recipe("cache-test-v2")),
            "artifact cache formula versions should invalidate recipe hashes");
    require(hash != cubey::procedural::procedural_artifact_recipe_hash(
                        test_recipe("cache-test-v1", 18U)),
            "artifact cache parameter changes should invalidate recipe hashes");
    require(cubey::procedural::procedural_artifact_payload_byte_count(recipe) == kPayload.size(),
            "artifact cache recipes should derive exact payload sizes");
}

void test_procedural_artifact_cache_round_trips_and_invalidates_entries() {
    CacheFixture fixture;
    cubey::procedural::ProceduralArtifactCache cache({.root = fixture.root});
    const cubey::procedural::ProceduralArtifactRecipe recipe = test_recipe();
    const cubey::procedural::ProceduralArtifactMetadata metadata = test_metadata(recipe, 0xfeedU);

    const auto miss = cache.load(recipe);
    require(miss.outcome == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Miss &&
                !miss.artifact.has_value(),
            "absent artifact cache entries should miss");
    const auto stored = cache.store(recipe, metadata, kPayload);
    require(stored.stored && stored.diagnostic.empty() &&
                std::filesystem::is_regular_file(stored.path),
            "artifact cache stores should publish an entry");
    for (const auto& entry : std::filesystem::directory_iterator(stored.path.parent_path())) {
        require(entry.path().extension() == ".cubey-artifact",
                "artifact cache stores should not leave temporary files behind");
    }

    const auto hit = cache.load(recipe);
    require(hit.outcome == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Hit &&
                hit.artifact.has_value(),
            "stored artifact cache entries should hit");
    require(hit.artifact->payload == std::vector<std::uint8_t>(kPayload.begin(), kPayload.end()),
            "artifact cache hits should preserve payload bytes");
    require(hit.artifact->metadata.content_hash == metadata.content_hash &&
                hit.artifact->metadata.formula_version == metadata.formula_version,
            "artifact cache hits should preserve metadata");

    const auto changed = cache.load(test_recipe("cache-test-v2"));
    require(changed.outcome == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Miss,
            "changed artifact recipes should address a different cache entry");
}

void test_procedural_artifact_cache_rejects_corrupt_entries() {
    CacheFixture fixture;
    cubey::procedural::ProceduralArtifactCache cache({.root = fixture.root});
    const cubey::procedural::ProceduralArtifactRecipe recipe = test_recipe();
    const auto stored = cache.store(recipe, test_metadata(recipe, 0xbeefU), kPayload);
    require(stored.stored, "corruption test should create an artifact cache entry");

    std::vector<std::uint8_t> bytes = cubey::read_binary_file(stored.path);
    bytes.back() ^= 0xffU;
    cubey::write_binary_file(stored.path, bytes);
    const auto corrupt = cache.load(recipe);
    require(corrupt.outcome == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Rejected &&
                !corrupt.diagnostic.empty() && !std::filesystem::exists(stored.path),
            "corrupt artifact cache entries should be rejected and removed");

    const auto restored = cache.store(recipe, test_metadata(recipe, 0xbeefU), kPayload);
    require(restored.stored, "artifact cache should replace a rejected entry");
    bytes = cubey::read_binary_file(restored.path);
    bytes.resize(bytes.size() - 3U);
    cubey::write_binary_file(restored.path, bytes);
    const auto truncated = cache.load(recipe);
    require(truncated.outcome == cubey::procedural::ProceduralArtifactCacheLoadOutcome::Rejected,
            "truncated artifact cache entries should be rejected");
}

void test_procedural_artifact_cache_prunes_oldest_entries() {
    CacheFixture fixture;
    cubey::procedural::ProceduralArtifactCache writer(
        {.root = fixture.root, .max_bytes = 1U << 20U});
    const auto first_recipe = test_recipe("cache-prune-v1", 1U);
    const auto second_recipe = test_recipe("cache-prune-v1", 2U);
    const auto third_recipe = test_recipe("cache-prune-v1", 3U);
    require(writer.store(first_recipe, test_metadata(first_recipe, 1U), kPayload).stored &&
                writer.store(second_recipe, test_metadata(second_recipe, 2U), kPayload).stored &&
                writer.store(third_recipe, test_metadata(third_recipe, 3U), kPayload).stored,
            "pruning test should create three cache entries");

    const auto first_path = writer.entry_path(first_recipe);
    const auto second_path = writer.entry_path(second_recipe);
    const auto third_path = writer.entry_path(third_recipe);
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(first_path, now - std::chrono::seconds{3});
    std::filesystem::last_write_time(second_path, now - std::chrono::seconds{2});
    std::filesystem::last_write_time(third_path, now - std::chrono::seconds{1});
    const std::uintmax_t keep_two_bytes =
        std::filesystem::file_size(second_path) + std::filesystem::file_size(third_path);

    cubey::procedural::ProceduralArtifactCache pruner(
        {.root = fixture.root, .max_bytes = keep_two_bytes});
    pruner.prune();
    require(!std::filesystem::exists(first_path) && std::filesystem::exists(second_path) &&
                std::filesystem::exists(third_path),
            "artifact cache pruning should remove oldest entries first");
}

void test_procedural_artifact_cache_failures_remain_nonfatal() {
    CacheFixture fixture;
    const std::filesystem::path blocked_root = fixture.root / "blocked";
    cubey::write_binary_file(blocked_root, kPayload);
    cubey::procedural::ProceduralArtifactCache cache({.root = blocked_root});
    const auto recipe = test_recipe();
    const auto failed = cache.store(recipe, test_metadata(recipe, 1U), kPayload);
    require(!failed.stored && !failed.diagnostic.empty(),
            "artifact cache write failures should return diagnostics without throwing");

    cubey::procedural::ProceduralArtifactCache tiny(
        {.root = fixture.root / "tiny", .max_bytes = 8U});
    const auto oversized = tiny.store(recipe, test_metadata(recipe, 1U), kPayload);
    require(!oversized.stored && !oversized.diagnostic.empty(),
            "artifact cache entries larger than the budget should be skipped without throwing");
}
