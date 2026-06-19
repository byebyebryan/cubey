#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace cubey::procedural {

inline constexpr std::uint64_t kProceduralFnv1a64Offset = 14695981039346656037ULL;
inline constexpr std::uint64_t kProceduralFnv1a64Prime = 1099511628211ULL;

class ProceduralHashBuilder {
  public:
    explicit ProceduralHashBuilder(
        std::uint64_t initial_value = kProceduralFnv1a64Offset);

    void append_byte(std::uint8_t value);
    void append_u32(std::uint32_t value);
    void append_u64(std::uint64_t value);
    void append_float32(float value);
    void append_string(std::string_view value);

    [[nodiscard]] std::uint64_t value() const;

  private:
    std::uint64_t value_ = kProceduralFnv1a64Offset;
};

[[nodiscard]] std::uint64_t procedural_hash_bytes(
    std::span<const std::uint8_t> bytes,
    std::uint64_t initial_value = kProceduralFnv1a64Offset);

} // namespace cubey::procedural
