#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace cubey {

[[nodiscard]] std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path);
void write_binary_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

} // namespace cubey
