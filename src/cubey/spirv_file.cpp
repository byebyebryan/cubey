#include <cubey/spirv_file.h>

#include <fstream>
#include <limits>
#include <stdexcept>

namespace cubey {

std::vector<std::uint32_t> read_spirv_file(const std::filesystem::path& path) {
    const std::uintmax_t byte_size = std::filesystem::file_size(path);
    if (byte_size == 0 || byte_size % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("invalid SPIR-V size for " + path.string());
    }
    if (byte_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        byte_size / sizeof(std::uint32_t) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("SPIR-V file is too large: " + path.string());
    }

    std::vector<std::uint32_t> code(static_cast<std::size_t>(byte_size / sizeof(std::uint32_t)));
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open SPIR-V file: " + path.string());
    }

    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(byte_size));
    if (!file) {
        throw std::runtime_error("failed to read SPIR-V file: " + path.string());
    }
    return code;
}

} // namespace cubey
