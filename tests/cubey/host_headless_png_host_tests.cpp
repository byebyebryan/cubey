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
    require(config.gpu_execution_mode == cubey::vulkan::GpuRuntimeExecutionMode::Threaded,
            "headless PNG host config should default to threaded GPU runtime");
    cubey::host::HeadlessPngHostCallbacks callbacks;
    require_throws([&] { cubey::host::HeadlessPngHost host(config, callbacks); },
                   "headless PNG host should require a record callback");

    callbacks.record_capture = [](cubey::host::HeadlessPngContext&, VkCommandBuffer,
                                  const cubey::host::HeadlessRenderTarget&) {};
    cubey::host::HeadlessPngHost host(config, callbacks);
    (void)host;

    static_assert(!HasSubmissionAccessor<cubey::host::HeadlessPngContext>);
    static_assert(
        std::is_same_v<decltype(&cubey::host::HeadlessPngContext::gpu),
                       cubey::vulkan::GpuRuntime& (cubey::host::HeadlessPngContext::*)() const>);
}
