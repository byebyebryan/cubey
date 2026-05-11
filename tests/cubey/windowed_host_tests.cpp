#include <cubey/app/windowed_host.h>

#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_windowed_host_config_defaults_to_two_frame_slots() {
    const cubey::app::WindowedHostConfig config;

    require(config.frame_slot_count == 2, "windowed host config should default to two frame slots");
    static_assert(std::is_same_v<decltype(config.frame_slot_count), std::uint32_t>);
    static_assert(std::is_same_v<decltype(&cubey::app::WindowedAppContext::submission),
                                 cubey::vulkan::SubmissionCoordinator& (
                                     cubey::app::WindowedAppContext::*)() const>);
    static_assert(
        std::is_same_v<decltype(&cubey::app::WindowedAppContext::gpu),
                       cubey::vulkan::GpuRuntime& (cubey::app::WindowedAppContext::*)() const>);
}
