#include <cubey/spirv_file.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_binary_file(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open temporary SPIR-V test file");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

} // namespace

void test_spirv_file_reads_aligned_words() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey_spirv_file_test.spv";
    std::filesystem::remove(path);

    constexpr std::array<std::uint32_t, 2> words{
        0x07230203U,
        0x00010000U,
    };
    const auto* bytes = reinterpret_cast<const unsigned char*>(words.data());
    write_binary_file(path, {bytes, bytes + sizeof(words)});

    const std::vector<std::uint32_t> loaded = cubey::read_spirv_file(path);
    require(loaded == std::vector<std::uint32_t>(words.begin(), words.end()),
            "SPIR-V loader should preserve 32-bit words");

    std::filesystem::remove(path);
}

void test_spirv_file_rejects_misaligned_byte_count() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey_spirv_file_bad_test.spv";
    std::filesystem::remove(path);
    write_binary_file(path, {0x03, 0x02, 0x23});

    bool rejected = false;
    try {
        static_cast<void>(cubey::read_spirv_file(path));
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(rejected, "SPIR-V loader should reject misaligned byte counts");
    std::filesystem::remove(path);
}
