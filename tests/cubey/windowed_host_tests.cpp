#include <cubey/host/windowed_app.h>
#include <cubey/host/windowed_host.h>

#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
concept HasSubmissionAccessor = requires { &T::submission; };

} // namespace

void test_windowed_host_config_defaults_to_two_frame_slots() {
    const cubey::host::WindowedHostConfig config;

    require(config.frame_slot_count == 2, "windowed host config should default to two frame slots");
    require(config.gpu_execution_mode == cubey::vulkan::GpuRuntimeExecutionMode::Threaded,
            "windowed host config should default to threaded GPU runtime");
    static_assert(std::is_same_v<decltype(config.frame_slot_count), std::uint32_t>);
    static_assert(!HasSubmissionAccessor<cubey::host::WindowedAppContext>);
    static_assert(
        std::is_same_v<decltype(&cubey::host::WindowedAppContext::gpu),
                       cubey::vulkan::GpuRuntime& (cubey::host::WindowedAppContext::*)() const>);
}

void test_windowed_app_config_preserves_windowed_host_defaults() {
    const cubey::host::WindowedAppConfig config;

    require(config.required_queue_flags == VK_QUEUE_GRAPHICS_BIT,
            "windowed app config should default to graphics queues");
    require(config.swapchain_image_usage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            "windowed app config should default to color attachment swapchain usage");
    require(config.frame_slot_count == 2, "windowed app config should default to two frame slots");
    require(config.gpu_execution_mode == cubey::vulkan::GpuRuntimeExecutionMode::Threaded,
            "windowed app config should default to threaded GPU runtime");
    require(!config.close_on_escape, "windowed app config should not force escape handling");
}
