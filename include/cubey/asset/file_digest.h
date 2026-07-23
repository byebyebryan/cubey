#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace cubey::asset {

[[nodiscard]] bool is_sha256_hex(std::string_view value) noexcept;
[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);

} // namespace cubey::asset
