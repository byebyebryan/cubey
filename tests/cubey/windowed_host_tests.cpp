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
