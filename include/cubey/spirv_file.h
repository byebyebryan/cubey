#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace cubey {

[[nodiscard]] std::vector<std::uint32_t> read_spirv_file(const std::filesystem::path& path);

} // namespace cubey
