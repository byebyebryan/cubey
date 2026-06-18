#pragma once

#include <cstdint>
#include <string_view>

namespace cubey::procedural {

[[nodiscard]] std::uint64_t stable_hash_string(std::string_view text);
[[nodiscard]] std::uint64_t derive_seed(std::uint64_t base_seed, std::string_view domain);
[[nodiscard]] std::uint64_t derive_seed(std::uint64_t base_seed, std::string_view domain,
                                        std::uint64_t salt);
[[nodiscard]] float random01(std::uint64_t base_seed, std::string_view domain,
                             std::uint32_t index, std::uint32_t channel);

} // namespace cubey::procedural
