#include <cubey/core/file_io.h>
#include <cubey/engine/video_encoder.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Fn>
void require_throws(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

void test_video_encoder_validates_config_and_frame_size() {
    cubey::validate_video_encoder_config({
        .output_path = "unit.mp4",
        .width = 2,
        .height = 2,
        .fps = 30,
    });

    require(cubey::video_frame_byte_size(2, 2) == 16,
            "video frame byte size should be width * height * rgba channels");
    cubey::validate_video_frame_size(2, 2, 16);

    require_throws(
        []() {
            cubey::validate_video_encoder_config({
                .output_path = "unit.mp4",
                .width = 0,
                .height = 2,
                .fps = 30,
            });
        },
        "video encoder should reject zero width");
    require_throws(
        []() {
            cubey::validate_video_encoder_config({
                .output_path = "unit.mp4",
                .width = 2,
                .height = 2,
                .fps = 0,
            });
        },
        "video encoder should reject zero fps");
    require_throws([]() { cubey::validate_video_frame_size(2, 2, 15); },
                   "video encoder should reject mismatched frame byte counts");
}

void test_video_encoder_writes_mp4_when_backend_is_available() {
    if (!cubey::video_encoding_available()) {
        return;
    }

    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "cubey_video_encoder_test.mp4";
    std::filesystem::remove(output);

    std::unique_ptr<cubey::VideoEncoder> encoder = cubey::create_video_encoder({
        .output_path = output,
        .width = 4,
        .height = 4,
        .fps = 12,
    });

    std::vector<std::uint8_t> red(cubey::video_frame_byte_size(4, 4), 255);
    for (std::size_t i = 0; i < red.size(); i += 4) {
        red[i + 0] = 255;
        red[i + 1] = 0;
        red[i + 2] = 0;
        red[i + 3] = 255;
    }
    std::vector<std::uint8_t> green(cubey::video_frame_byte_size(4, 4), 255);
    for (std::size_t i = 0; i < green.size(); i += 4) {
        green[i + 0] = 0;
        green[i + 1] = 255;
        green[i + 2] = 0;
        green[i + 3] = 255;
    }

    encoder->write_frame(red);
    encoder->write_frame(green);
    encoder->finish();

    const std::vector<std::uint8_t> bytes = cubey::read_binary_file(output);
    constexpr std::array<std::uint8_t, 4> ftyp{'f', 't', 'y', 'p'};
    require(bytes.size() > 16, "video encoder should write a nonempty MP4 file");
    require(std::search(bytes.begin(), bytes.end(), ftyp.begin(), ftyp.end()) != bytes.end(),
            "video encoder output should contain an MP4 file type box");

    std::filesystem::remove(output);
}
