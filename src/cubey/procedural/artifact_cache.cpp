#include <cubey/procedural/artifact_cache.h>

#include <cubey/core/file_io.h>
#include <cubey/procedural/hash.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace cubey::procedural {
namespace {

constexpr std::array<std::uint8_t, 8> kCacheMagic{'C', 'U', 'B', 'E', 'Y', 'P', 'C', '1'};
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::size_t kMaximumHeaderBytes = 64U * 1024U;
constexpr std::string_view kEntryExtension = ".cubey-artifact";

[[nodiscard]] std::size_t format_bytes_per_sample(ProceduralArtifactValueFormat format) {
    switch (format) {
    case ProceduralArtifactValueFormat::Rgba8Unorm:
        return 4U;
    case ProceduralArtifactValueFormat::Rgba32Float:
        return sizeof(float) * 4U;
    case ProceduralArtifactValueFormat::ScalarFloat32:
        return sizeof(float);
    case ProceduralArtifactValueFormat::ScalarUInt8:
        return sizeof(std::uint8_t);
    case ProceduralArtifactValueFormat::OpaqueBytes:
        throw std::runtime_error(
            "opaque procedural artifacts do not have an extent-derived payload size");
    case ProceduralArtifactValueFormat::Unknown:
        break;
    }
    throw std::runtime_error("procedural artifact cache recipe has unknown value format");
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t byte_index = 0U; byte_index < 4U; ++byte_index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xffU));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t byte_index = 0U; byte_index < 8U; ++byte_index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xffU));
    }
}

void append_string(std::vector<std::uint8_t>& bytes, std::string_view value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("procedural artifact cache string is too large");
    }
    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void overwrite_u64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    if (offset + sizeof(value) > bytes.size()) {
        throw std::runtime_error("procedural artifact cache header offset is invalid");
    }
    for (std::uint32_t byte_index = 0U; byte_index < 8U; ++byte_index) {
        bytes[offset + byte_index] =
            static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xffU);
    }
}

class ByteReader {
  public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::span<const std::uint8_t> take(std::size_t count) {
        if (count > bytes_.size() - offset_) {
            throw std::runtime_error("procedural artifact cache entry is truncated");
        }
        const std::span<const std::uint8_t> result = bytes_.subspan(offset_, count);
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::uint32_t read_u32() {
        const std::span<const std::uint8_t> value = take(4U);
        std::uint32_t result = 0U;
        for (std::uint32_t byte_index = 0U; byte_index < 4U; ++byte_index) {
            result |= static_cast<std::uint32_t>(value[byte_index]) << (byte_index * 8U);
        }
        return result;
    }

    [[nodiscard]] std::uint64_t read_u64() {
        const std::span<const std::uint8_t> value = take(8U);
        std::uint64_t result = 0U;
        for (std::uint32_t byte_index = 0U; byte_index < 8U; ++byte_index) {
            result |= static_cast<std::uint64_t>(value[byte_index]) << (byte_index * 8U);
        }
        return result;
    }

    [[nodiscard]] std::string read_string() {
        const std::uint32_t count = read_u32();
        const std::span<const std::uint8_t> value = take(count);
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    }

    [[nodiscard]] std::size_t offset() const noexcept {
        return offset_;
    }

  private:
    std::span<const std::uint8_t> bytes_{};
    std::size_t offset_ = 0U;
};

[[nodiscard]] std::string sanitize_path_component(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        const bool accepted = (character >= 'a' && character <= 'z') ||
                              (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9') || character == '-' ||
                              character == '_' || character == '.';
        result.push_back(accepted ? character : '_');
    }
    return result.empty() ? std::string{"artifact"} : result;
}

[[nodiscard]] std::string hexadecimal_hash(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

[[nodiscard]] std::filesystem::path temporary_path_for(const std::filesystem::path& target) {
    std::random_device random;
    const std::uint64_t nonce =
        (static_cast<std::uint64_t>(random()) << 32U) ^ static_cast<std::uint64_t>(random());
    return target.string() + ".tmp." + hexadecimal_hash(nonce);
}

void validate_metadata_matches_recipe(const ProceduralArtifactRecipe& recipe,
                                      const ProceduralArtifactMetadata& metadata) {
    validate_procedural_artifact_metadata(metadata);
    if (metadata.name != recipe.name || metadata.generator != recipe.generator ||
        metadata.formula_version != recipe.formula_version || metadata.domain != recipe.domain ||
        metadata.seed != recipe.seed || metadata.space != recipe.space ||
        metadata.kind != recipe.kind || metadata.format != recipe.format ||
        metadata.extent.width != recipe.extent.width ||
        metadata.extent.height != recipe.extent.height ||
        metadata.extent.depth != recipe.extent.depth ||
        metadata.extent.faces != recipe.extent.faces ||
        metadata.extent.mip_levels != recipe.extent.mip_levels) {
        throw std::runtime_error("procedural artifact metadata does not match its cache recipe");
    }
}

[[nodiscard]] std::vector<std::uint8_t> encode_entry(const ProceduralArtifactRecipe& recipe,
                                                     const ProceduralArtifactMetadata& metadata,
                                                     std::span<const std::uint8_t> payload) {
    validate_metadata_matches_recipe(recipe, metadata);
    if (!procedural_artifact_payload_size_matches(recipe, payload.size())) {
        throw std::runtime_error("procedural artifact cache payload size does not match recipe");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kMaximumHeaderBytes + payload.size());
    bytes.insert(bytes.end(), kCacheMagic.begin(), kCacheMagic.end());
    append_u32(bytes, kProceduralArtifactCacheSchemaVersion);
    append_u32(bytes, kEndianMarker);
    const std::size_t header_bytes_offset = bytes.size();
    append_u64(bytes, 0U);
    append_u64(bytes, static_cast<std::uint64_t>(payload.size()));
    append_u64(bytes, procedural_artifact_recipe_hash(recipe));
    append_u64(bytes, procedural_hash_bytes(payload));
    append_u64(bytes, metadata.content_hash);
    append_u64(bytes, recipe.seed);
    append_u64(bytes, recipe.parameter_hash);
    append_u32(bytes, static_cast<std::uint32_t>(recipe.space));
    append_u32(bytes, static_cast<std::uint32_t>(recipe.kind));
    append_u32(bytes, static_cast<std::uint32_t>(recipe.format));
    append_u32(bytes, recipe.extent.width);
    append_u32(bytes, recipe.extent.height);
    append_u32(bytes, recipe.extent.depth);
    append_u32(bytes, recipe.extent.faces);
    append_u32(bytes, recipe.extent.mip_levels);
    append_string(bytes, recipe.name);
    append_string(bytes, recipe.generator);
    append_string(bytes, recipe.formula_version);
    append_string(bytes, recipe.domain);
    if (bytes.size() > kMaximumHeaderBytes) {
        throw std::runtime_error("procedural artifact cache header is too large");
    }
    overwrite_u64(bytes, header_bytes_offset, static_cast<std::uint64_t>(bytes.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

[[nodiscard]] CachedProceduralArtifact decode_entry(const ProceduralArtifactRecipe& expected_recipe,
                                                    std::span<const std::uint8_t> bytes) {
    ByteReader reader(bytes);
    if (!std::ranges::equal(reader.take(kCacheMagic.size()), kCacheMagic)) {
        throw std::runtime_error("procedural artifact cache magic is invalid");
    }
    if (reader.read_u32() != kProceduralArtifactCacheSchemaVersion) {
        throw std::runtime_error("procedural artifact cache schema is incompatible");
    }
    if (reader.read_u32() != kEndianMarker) {
        throw std::runtime_error("procedural artifact cache byte order is incompatible");
    }
    const std::uint64_t header_bytes = reader.read_u64();
    const std::uint64_t payload_bytes = reader.read_u64();
    const std::uint64_t recipe_hash = reader.read_u64();
    const std::uint64_t payload_hash = reader.read_u64();
    const std::uint64_t content_hash = reader.read_u64();

    ProceduralArtifactRecipe stored_recipe{
        .seed = reader.read_u64(),
        .parameter_hash = reader.read_u64(),
        .space = static_cast<ProceduralDomainSpace>(reader.read_u32()),
        .kind = static_cast<ProceduralArtifactKind>(reader.read_u32()),
        .format = static_cast<ProceduralArtifactValueFormat>(reader.read_u32()),
        .extent =
            {
                .width = reader.read_u32(),
                .height = reader.read_u32(),
                .depth = reader.read_u32(),
                .faces = reader.read_u32(),
                .mip_levels = reader.read_u32(),
            },
    };
    stored_recipe.name = reader.read_string();
    stored_recipe.generator = reader.read_string();
    stored_recipe.formula_version = reader.read_string();
    stored_recipe.domain = reader.read_string();
    validate_procedural_artifact_recipe(stored_recipe);

    if (header_bytes != reader.offset() || header_bytes > kMaximumHeaderBytes ||
        header_bytes > bytes.size() || payload_bytes != bytes.size() - header_bytes ||
        payload_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("procedural artifact cache entry layout is invalid");
    }
    if (recipe_hash != procedural_artifact_recipe_hash(expected_recipe) ||
        recipe_hash != procedural_artifact_recipe_hash(stored_recipe)) {
        throw std::runtime_error("procedural artifact cache recipe does not match request");
    }
    if (stored_recipe.name != expected_recipe.name ||
        stored_recipe.generator != expected_recipe.generator ||
        stored_recipe.formula_version != expected_recipe.formula_version ||
        stored_recipe.domain != expected_recipe.domain ||
        stored_recipe.seed != expected_recipe.seed ||
        stored_recipe.parameter_hash != expected_recipe.parameter_hash ||
        stored_recipe.space != expected_recipe.space ||
        stored_recipe.kind != expected_recipe.kind ||
        stored_recipe.format != expected_recipe.format ||
        stored_recipe.extent.width != expected_recipe.extent.width ||
        stored_recipe.extent.height != expected_recipe.extent.height ||
        stored_recipe.extent.depth != expected_recipe.extent.depth ||
        stored_recipe.extent.faces != expected_recipe.extent.faces ||
        stored_recipe.extent.mip_levels != expected_recipe.extent.mip_levels) {
        throw std::runtime_error("procedural artifact cache recipe fields do not match request");
    }

    const std::span<const std::uint8_t> payload = bytes.subspan(
        static_cast<std::size_t>(header_bytes), static_cast<std::size_t>(payload_bytes));
    if (!procedural_artifact_payload_size_matches(expected_recipe, payload.size()) ||
        procedural_hash_bytes(payload) != payload_hash) {
        throw std::runtime_error("procedural artifact cache payload is corrupt");
    }

    CachedProceduralArtifact artifact;
    artifact.metadata = make_procedural_artifact_metadata(
        make_procedural_artifact_identity(stored_recipe.name, stored_recipe.generator,
                                          stored_recipe.formula_version, stored_recipe.domain,
                                          stored_recipe.seed, stored_recipe.space),
        stored_recipe.kind, stored_recipe.format, stored_recipe.extent, content_hash);
    artifact.payload.assign(payload.begin(), payload.end());
    return artifact;
}

[[nodiscard]] std::string exception_message() {
    try {
        throw;
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "unknown procedural artifact cache failure";
    }
}

} // namespace

void validate_procedural_artifact_recipe(const ProceduralArtifactRecipe& recipe) {
    validate_procedural_artifact_metadata(ProceduralArtifactMetadata{
        .name = recipe.name,
        .generator = recipe.generator,
        .formula_version = recipe.formula_version,
        .domain = recipe.domain,
        .seed = recipe.seed,
        .space = recipe.space,
        .kind = recipe.kind,
        .format = recipe.format,
        .extent = recipe.extent,
    });
    if (recipe.format != ProceduralArtifactValueFormat::OpaqueBytes) {
        static_cast<void>(procedural_artifact_payload_byte_count(recipe));
    }
}

std::uint64_t procedural_artifact_recipe_hash(const ProceduralArtifactRecipe& recipe) {
    validate_procedural_artifact_recipe(recipe);
    ProceduralHashBuilder hash;
    hash.append_string("cubey.procedural-artifact-cache.recipe.v1");
    hash.append_string(recipe.name);
    hash.append_string(recipe.generator);
    hash.append_string(recipe.formula_version);
    hash.append_string(recipe.domain);
    hash.append_u64(recipe.seed);
    hash.append_u64(recipe.parameter_hash);
    hash.append_u32(static_cast<std::uint32_t>(recipe.space));
    hash.append_u32(static_cast<std::uint32_t>(recipe.kind));
    hash.append_u32(static_cast<std::uint32_t>(recipe.format));
    hash.append_u32(recipe.extent.width);
    hash.append_u32(recipe.extent.height);
    hash.append_u32(recipe.extent.depth);
    hash.append_u32(recipe.extent.faces);
    hash.append_u32(recipe.extent.mip_levels);
    return hash.value();
}

std::size_t procedural_artifact_payload_byte_count(const ProceduralArtifactRecipe& recipe) {
    const std::size_t samples = procedural_artifact_sample_count(recipe.extent);
    const std::size_t bytes_per_sample = format_bytes_per_sample(recipe.format);
    if (samples > std::numeric_limits<std::size_t>::max() / bytes_per_sample) {
        throw std::runtime_error("procedural artifact cache payload size overflows");
    }
    return samples * bytes_per_sample;
}

bool procedural_artifact_payload_size_matches(const ProceduralArtifactRecipe& recipe,
                                              std::size_t payload_bytes) {
    if (recipe.format == ProceduralArtifactValueFormat::OpaqueBytes) {
        return payload_bytes != 0U;
    }
    return payload_bytes == procedural_artifact_payload_byte_count(recipe);
}

ProceduralArtifactCache::ProceduralArtifactCache(ProceduralArtifactCacheConfig config)
    : config_(std::move(config)) {
    if (config_.root.empty()) {
        throw std::runtime_error("procedural artifact cache root must be non-empty");
    }
    if (config_.max_bytes == 0U) {
        throw std::runtime_error("procedural artifact cache budget must be positive");
    }
}

const std::filesystem::path& ProceduralArtifactCache::root() const noexcept {
    return config_.root;
}

std::filesystem::path
ProceduralArtifactCache::entry_path(const ProceduralArtifactRecipe& recipe) const {
    const std::uint64_t recipe_hash = procedural_artifact_recipe_hash(recipe);
    const std::string filename = sanitize_path_component(recipe.formula_version) + "-" +
                                 hexadecimal_hash(recipe_hash) + std::string{kEntryExtension};
    return config_.root / sanitize_path_component(recipe.domain) / filename;
}

ProceduralArtifactCacheLoadResult
ProceduralArtifactCache::load(const ProceduralArtifactRecipe& recipe) {
    ProceduralArtifactCacheLoadResult result{
        .path = entry_path(recipe),
    };
    std::error_code error;
    if (!std::filesystem::exists(result.path, error)) {
        if (error) {
            result.outcome = ProceduralArtifactCacheLoadOutcome::Rejected;
            result.diagnostic =
                "failed to inspect procedural artifact cache entry: " + error.message();
        }
        return result;
    }

    try {
        const std::uintmax_t file_bytes = std::filesystem::file_size(result.path);
        if (recipe.format == ProceduralArtifactValueFormat::OpaqueBytes) {
            if (file_bytes > config_.max_bytes) {
                throw std::runtime_error("procedural artifact cache entry exceeds cache budget");
            }
        } else {
            const std::uintmax_t expected_payload =
                procedural_artifact_payload_byte_count(recipe);
            if (file_bytes > expected_payload + kMaximumHeaderBytes) {
                throw std::runtime_error("procedural artifact cache entry is larger than expected");
            }
        }
        const std::vector<std::uint8_t> bytes = cubey::read_binary_file(result.path);
        result.artifact.emplace(decode_entry(recipe, bytes));
        result.outcome = ProceduralArtifactCacheLoadOutcome::Hit;
        std::filesystem::last_write_time(result.path, std::filesystem::file_time_type::clock::now(),
                                         error);
    } catch (...) {
        result.outcome = ProceduralArtifactCacheLoadOutcome::Rejected;
        result.artifact.reset();
        result.diagnostic = exception_message();
        std::filesystem::remove(result.path, error);
    }
    return result;
}

ProceduralArtifactCacheStoreResult
ProceduralArtifactCache::store(const ProceduralArtifactRecipe& recipe,
                               const ProceduralArtifactMetadata& metadata,
                               std::span<const std::uint8_t> payload) {
    ProceduralArtifactCacheStoreResult result{
        .path = entry_path(recipe),
    };
    std::filesystem::path temporary;
    try {
        const std::vector<std::uint8_t> bytes = encode_entry(recipe, metadata, payload);
        if (bytes.size() > config_.max_bytes) {
            result.diagnostic = "procedural artifact cache entry exceeds cache budget";
            return result;
        }
        std::filesystem::create_directories(result.path.parent_path());
        temporary = temporary_path_for(result.path);
        cubey::write_binary_file(temporary, bytes);
        std::filesystem::rename(temporary, result.path);
        result.stored = true;
        prune();
    } catch (...) {
        result.diagnostic = exception_message();
        if (!temporary.empty()) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
        }
    }
    return result;
}

void ProceduralArtifactCache::prune() {
    struct Entry {
        std::filesystem::path path{};
        std::uintmax_t bytes = 0U;
        std::filesystem::file_time_type modified{};
    };

    std::error_code error;
    if (!std::filesystem::exists(config_.root, error) || error) {
        return;
    }
    std::vector<Entry> entries;
    std::uintmax_t total_bytes = 0U;
    for (std::filesystem::recursive_directory_iterator iterator(
             config_.root, std::filesystem::directory_options::skip_permission_denied, error),
         end;
         iterator != end; iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!iterator->is_regular_file(error) || error ||
            iterator->path().extension() != kEntryExtension) {
            error.clear();
            continue;
        }
        const std::uintmax_t bytes = iterator->file_size(error);
        if (error) {
            error.clear();
            continue;
        }
        const std::filesystem::file_time_type modified = iterator->last_write_time(error);
        if (error) {
            error.clear();
            continue;
        }
        entries.push_back({iterator->path(), bytes, modified});
        total_bytes += bytes;
    }
    std::ranges::sort(entries, {}, &Entry::modified);
    for (const Entry& entry : entries) {
        if (total_bytes <= config_.max_bytes) {
            break;
        }
        if (std::filesystem::remove(entry.path, error)) {
            total_bytes -= entry.bytes;
        }
        error.clear();
    }
}

std::filesystem::path default_procedural_artifact_cache_root() {
    return std::filesystem::path{CUBEY_PROCEDURAL_CACHE_ROOT};
}

} // namespace cubey::procedural
