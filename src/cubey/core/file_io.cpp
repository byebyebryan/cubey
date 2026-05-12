#include <cubey/core/file_io.h>

#include <fstream>
#include <limits>
#include <stdexcept>

namespace cubey {

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    const std::uintmax_t byte_size = std::filesystem::file_size(path);
    if (byte_size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        byte_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("binary file is too large: " + path.string());
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byte_size));
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open binary file: " + path.string());
    }

    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(byte_size));
        if (!file) {
            throw std::runtime_error("failed to read binary file: " + path.string());
        }
    }
    return bytes;
}

void write_binary_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("binary file is too large to write: " + path.string());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open binary file for writing: " + path.string());
    }

    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            throw std::runtime_error("failed to write binary file: " + path.string());
        }
    }
}

} // namespace cubey
