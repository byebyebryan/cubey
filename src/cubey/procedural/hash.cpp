#include <cubey/procedural/hash.h>

#include <bit>

namespace cubey::procedural {

ProceduralHashBuilder::ProceduralHashBuilder(std::uint64_t initial_value)
    : value_(initial_value) {}

void ProceduralHashBuilder::append_byte(std::uint8_t value) {
    value_ ^= value;
    value_ *= kProceduralFnv1a64Prime;
}

void ProceduralHashBuilder::append_u32(std::uint32_t value) {
    for (std::uint32_t byte_index = 0; byte_index < 4U; ++byte_index) {
        append_byte(static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xffU));
    }
}

void ProceduralHashBuilder::append_u64(std::uint64_t value) {
    for (std::uint32_t byte_index = 0; byte_index < 8U; ++byte_index) {
        append_byte(static_cast<std::uint8_t>((value >> (byte_index * 8U)) & 0xffU));
    }
}

void ProceduralHashBuilder::append_float32(float value) {
    append_u32(std::bit_cast<std::uint32_t>(value));
}

void ProceduralHashBuilder::append_string(std::string_view value) {
    append_u64(static_cast<std::uint64_t>(value.size()));
    for (const char byte : value) {
        append_byte(static_cast<std::uint8_t>(byte));
    }
}

std::uint64_t ProceduralHashBuilder::value() const { return value_; }

std::uint64_t procedural_hash_bytes(std::span<const std::uint8_t> bytes,
                                    std::uint64_t initial_value) {
    ProceduralHashBuilder hash(initial_value);
    for (const std::uint8_t byte : bytes) {
        hash.append_byte(byte);
    }
    return hash.value();
}

} // namespace cubey::procedural
