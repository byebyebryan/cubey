#include <cubey/core/file_io.h>

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

void test_file_io_round_trips_binary_bytes() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cubey_file_io_test.bin";
    std::filesystem::remove(path);

    constexpr std::array<std::uint8_t, 6> bytes{
        0x00, 0x01, 0x02, 0x80, 0xFE, 0xFF,
    };
    cubey::write_binary_file(path, bytes);

    const std::vector<std::uint8_t> loaded = cubey::read_binary_file(path);
    require(loaded == std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
            "binary file I/O should preserve bytes exactly");

    std::filesystem::remove(path);
}
