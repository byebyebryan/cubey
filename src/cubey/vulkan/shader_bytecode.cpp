#include <cubey/vulkan/shader_bytecode.h>

#include <cubey/core/file_io.h>

#include <cstring>
#include <stdexcept>

namespace cubey::vulkan {

std::vector<std::uint32_t> read_spirv_file(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = read_binary_file(path);
    if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("invalid SPIR-V size for " + path.string());
    }

    std::vector<std::uint32_t> code(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(code.data(), bytes.data(), bytes.size());
    return code;
}

} // namespace cubey::vulkan
