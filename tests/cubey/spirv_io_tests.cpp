#include <cubey/core/file_io.h>
#include <cubey/core/spirv_io.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_spirv_io_reads_aligned_words() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey_spirv_io_test.spv";
    std::filesystem::remove(path);

    constexpr std::array<std::uint32_t, 2> words{
        0x07230203U,
        0x00010000U,
    };
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(words.data());
    const std::vector<std::uint8_t> spirv_bytes{bytes, bytes + sizeof(words)};
    cubey::write_binary_file(path, spirv_bytes);

    const std::vector<std::uint32_t> loaded = cubey::read_spirv_file(path);
    require(loaded == std::vector<std::uint32_t>(words.begin(), words.end()),
            "SPIR-V loader should preserve 32-bit words");

    std::filesystem::remove(path);
}

void test_spirv_io_rejects_misaligned_byte_count() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey_spirv_io_bad_test.spv";
    std::filesystem::remove(path);
    constexpr std::array<std::uint8_t, 3> bytes{0x03, 0x02, 0x23};
    cubey::write_binary_file(path, bytes);

    bool rejected = false;
    try {
        static_cast<void>(cubey::read_spirv_file(path));
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(rejected, "SPIR-V loader should reject misaligned byte counts");
    std::filesystem::remove(path);
}
