#include <cubey/procedural/seed.h>

namespace cubey::procedural {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint64_t kSeedDomainSalt = 0x9e37'79b9'7f4a'7c15ULL;
constexpr std::uint64_t kSeedSaltSalt = 0xbf58'476d'1ce4'e5b9ULL;

[[nodiscard]] std::uint64_t mix_u64(std::uint64_t value) {
    value += kSeedDomainSalt;
    value = (value ^ (value >> 30U)) * 0xbf58'476d'1ce4'e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d0'49bb'1331'11ebULL;
    return value ^ (value >> 31U);
}

} // namespace

std::uint64_t stable_hash_string(std::string_view text) {
    std::uint64_t hash = kFnvOffset;
    for (const char byte : text) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t derive_seed(std::uint64_t base_seed, std::string_view domain) {
    return mix_u64(base_seed ^ stable_hash_string(domain));
}

std::uint64_t derive_seed(std::uint64_t base_seed, std::string_view domain, std::uint64_t salt) {
    return mix_u64(derive_seed(base_seed, domain) ^ mix_u64(salt + kSeedSaltSalt));
}

float random01(std::uint64_t base_seed, std::string_view domain, std::uint32_t index,
               std::uint32_t channel) {
    const std::uint64_t salt = (static_cast<std::uint64_t>(index) << 32U) |
                               static_cast<std::uint64_t>(channel);
    constexpr float kInv24Bit = 1.0F / 16'777'215.0F;
    return static_cast<float>(derive_seed(base_seed, domain, salt) >> 40U) * kInv24Bit;
}

} // namespace cubey::procedural
