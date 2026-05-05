#include <cubey/capture_queue.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open generated capture");
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

void test_capture_queue_encodes_png_with_inline_executor() {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cubey_capture_queue_inline.png";
    std::filesystem::remove(output);

    cubey::jobs::InlineExecutor jobs;
    cubey::CaptureQueue queue(jobs);
    cubey::CaptureTicket ticket = queue.enqueue_png({
        .output_path = output,
        .width = 2,
        .height = 2,
        .rgba8 = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255},
    });

    require(ticket.ready(), "inline capture queue should finish before returning");
    ticket.finish();

    const std::vector<std::uint8_t> bytes = read_file(output);
    constexpr std::array<std::uint8_t, 8> png_signature{
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
    };
    require(bytes.size() > png_signature.size(), "capture PNG should contain payload");
    require(std::equal(png_signature.begin(), png_signature.end(), bytes.begin()),
            "capture queue should write a PNG artifact");

    std::filesystem::remove(output);
}

void test_capture_queue_propagates_encoding_errors() {
    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cubey_capture_queue_invalid.png";
    std::filesystem::remove(output);

    cubey::jobs::InlineExecutor jobs;
    cubey::CaptureQueue queue(jobs);
    cubey::CaptureTicket ticket = queue.enqueue_png({
        .output_path = output,
        .width = 2,
        .height = 2,
        .rgba8 = {255, 0, 0, 255},
    });

    bool propagated = false;
    try {
        ticket.finish();
    } catch (const std::runtime_error&) {
        propagated = true;
    }

    require(propagated, "capture ticket should propagate PNG encoding failures");
}
