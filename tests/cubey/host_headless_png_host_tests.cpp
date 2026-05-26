#include <cubey/host/headless_png_host.h>

#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Func> void require_throws(Func&& func, const char* message) {
    bool threw = false;
    try {
        func();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, message);
}

template <typename T>
concept HasSubmissionAccessor = requires { &T::submission; };

} // namespace

void test_headless_png_host_validates_capture_shape() {
    require(cubey::host::headless_png_byte_size(2, 3) == 24,
            "headless PNG byte size should cover RGBA8 pixels");
    require_throws([] { static_cast<void>(cubey::host::headless_png_byte_size(0, 1)); },
                   "headless PNG byte size should reject zero width");
    require_throws([] { static_cast<void>(cubey::host::headless_png_byte_size(1, 0)); },
                   "headless PNG byte size should reject zero height");

    cubey::host::HeadlessPngHostConfig config;
    config.run_config.title = "headless-png-host-test";
    require(config.output_format == VK_FORMAT_R8G8B8A8_SRGB,
            "headless PNG host should default to an sRGB RGBA8 render target");
    require(config.gpu_execution_mode == cubey::vulkan::GpuRuntimeExecutionMode::Threaded,
            "headless PNG host config should default to threaded GPU runtime");
    cubey::host::HeadlessPngHostCallbacks callbacks;
    require_throws([&] { cubey::host::HeadlessPngHost host(config, callbacks); },
                   "headless PNG host should require a record callback");

    callbacks.record_capture = [](cubey::host::HeadlessPngContext&, VkCommandBuffer,
                                  const cubey::host::HeadlessRenderTarget&) {};
    cubey::host::HeadlessPngHost host(config, callbacks);
    (void)host;

    config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
    cubey::host::HeadlessPngHost unorm_host(config, callbacks);
    (void)unorm_host;

    static_assert(!HasSubmissionAccessor<cubey::host::HeadlessPngContext>);
    static_assert(
        std::is_same_v<decltype(&cubey::host::HeadlessPngContext::gpu),
                       cubey::vulkan::GpuRuntime& (cubey::host::HeadlessPngContext::*)() const>);
}

void test_headless_capture_frame_helpers_select_png_or_video_timing() {
    cubey::RunConfig config;
    config.capture_mode = cubey::CaptureMode::Png;
    config.frames = 99;
    config.fps = 24;
    require(cubey::host::headless_capture_frame_count(config) == 1,
            "PNG capture should always render one output frame");

    const cubey::host::HeadlessCaptureFrame png_frame =
        cubey::host::headless_capture_frame(config, 0);
    require(png_frame.index == 0, "PNG capture frame should start at index zero");
    require(png_frame.count == 1, "PNG capture frame should report one frame");
    require(png_frame.frame_slot.index == 0, "PNG capture should use frame slot zero");
    require(png_frame.frame_slot.count == 1, "PNG capture should use a single frame slot");
    require(png_frame.timing.frame_index == 0, "PNG timing should use frame index zero");
    require(png_frame.timing.delta_seconds == 0.0, "PNG timing should be static");

    config.capture_mode = cubey::CaptureMode::Video;
    config.frames = 0;
    config.fps = 30;
    require(cubey::host::headless_capture_frame_count(config) == 300,
            "video capture should resolve zero frames to the default duration");
    require(cubey::host::headless_capture_frame_slot_count(config) == 3,
            "video capture should use a small in-flight slot ring");

    config.frames = 12;
    const cubey::host::HeadlessCaptureFrame video_frame =
        cubey::host::headless_capture_frame(config, 3);
    require(video_frame.index == 3, "video capture frame should preserve frame index");
    require(video_frame.count == 12, "video capture frame should report configured count");
    require(video_frame.frame_slot.index == 0,
            "video frame slot should wrap through the capture slot ring");
    require(video_frame.frame_slot.count == 3,
            "video frame slot should report capture slot ring count");
    require(video_frame.timing.frame_index == 3, "video timing should mirror frame index");
    require(video_frame.timing.delta_seconds == 1.0 / 30.0,
            "video timing should use fixed fps delta");
    require(video_frame.timing.elapsed_seconds == 3.0 / 30.0,
            "video timing should use deterministic elapsed time");

    const cubey::FrameTiming simulation_timing =
        cubey::host::headless_video_simulation_timing(video_frame);
    require(simulation_timing.frame_index == 4,
            "video simulation timing should advance to one-based frame indices");
    require(simulation_timing.elapsed_seconds == 4.0 / 30.0,
            "video simulation timing should point at the simulated frame end");

    const cubey::host::HeadlessCaptureFrame simulation_frame =
        cubey::host::headless_simulation_frame(config, 4, 12, simulation_timing);
    require(simulation_frame.index == 4, "simulation frame should preserve frame index");
    require(simulation_frame.count == 12, "simulation frame should preserve frame count");
    require(simulation_frame.frame_slot.index == 1,
            "simulation frame should use the same slot ring policy as capture");
    require(simulation_frame.timing.frame_index == 4,
            "simulation frame should carry caller-provided timing");
    require_throws(
        [&] {
            static_cast<void>(cubey::host::headless_simulation_frame(
                config, 0, 0, simulation_timing));
        },
        "simulation frame helper should reject zero frame count");
    require_throws(
        [&] {
            static_cast<void>(cubey::host::headless_simulation_frame(
                config, 12, 12, simulation_timing));
        },
        "simulation frame helper should reject out-of-range frame indices");
    require_throws(
        [&] {
            static_cast<void>(cubey::host::headless_video_simulation_timing(png_frame));
        },
        "video simulation timing should reject static PNG timing");
}
