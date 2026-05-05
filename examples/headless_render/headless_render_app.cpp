#include "headless_render_app.h"

#include <cubey/image_output.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/rendering.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::examples::headless_render {
namespace {

using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

constexpr VkFormat kOutputFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::size_t kOutputBytesPerPixel = 4;

[[nodiscard]] std::size_t checked_pixel_byte_size(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("headless render dimensions must be positive");
    }

    const std::size_t checked_width = static_cast<std::size_t>(width);
    const std::size_t checked_height = static_cast<std::size_t>(height);
    if (checked_width > std::numeric_limits<std::size_t>::max() / checked_height) {
        throw std::runtime_error("headless render output is too large");
    }

    const std::size_t pixel_count = checked_width * checked_height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / kOutputBytesPerPixel) {
        throw std::runtime_error("headless render output is too large");
    }
    return pixel_count * kOutputBytesPerPixel;
}

class HeadlessRenderApp {
  public:
    explicit HeadlessRenderApp(RunConfig config) : config_(std::move(config)) {}

    HeadlessRenderApp(const HeadlessRenderApp&) = delete;
    HeadlessRenderApp& operator=(const HeadlessRenderApp&) = delete;

    int run() {
        create_instance();
        create_device();
        render_png();
        return 0;
    }

  private:
    void create_instance() {
        cubey::vulkan::InstanceConfig instance_config;
        instance_config.application_name = config_.title;
        instance_config.validation = config_.validation;
        instance_config.require_validation = config_.require_validation;
        instance_.emplace(instance_config);
    }

    void create_device() {
        cubey::vulkan::DeviceConfig device_config;
        device_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
        device_config.require_present = false;
        device_config.require_dynamic_rendering = true;

        device_.emplace(vulkan_instance(), device_config);
    }

    void render_png() {
        const VkExtent2D extent{config_.width, config_.height};
        cubey::vulkan::Image render_target(
            vulkan_device(),
            cubey::vulkan::color_render_target_image_config(extent, kOutputFormat));
        record_clear_to_image(render_target, extent);

        const std::size_t byte_size = checked_pixel_byte_size(extent.width, extent.height);
        const VkDeviceSize readback_byte_size = static_cast<VkDeviceSize>(byte_size);
        cubey::vulkan::Buffer readback(vulkan_device(),
                                       cubey::vulkan::readback_buffer_config(readback_byte_size));
        cubey::vulkan::copy_image_to_buffer(vulkan_device(), render_target.handle(),
                                            readback.handle(), {extent.width, extent.height, 1});

        std::vector<std::uint8_t> pixels(byte_size);
        readback.download(pixels.data(), readback_byte_size);
        cubey::write_png_rgba8(config_.output_path, extent.width, extent.height, pixels);

        const std::string output_path = config_.output_path.string();
        std::printf("headless_render: %s wrote %s at %ux%u\n", vulkan_device().device_name(),
                    output_path.c_str(), extent.width, extent.height);
        check(vkDeviceWaitIdle(vulkan_device().handle()), "vkDeviceWaitIdle after headless_render");
    }

    void record_clear_to_image(cubey::vulkan::Image& render_target, VkExtent2D extent) {
        cubey::vulkan::ImmediateCommands commands(vulkan_device());
        const VkCommandBuffer command_buffer = commands.command_buffer();

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::begin_color_attachment_transition(render_target.handle()));

        VkClearValue clear{};
        clear.color = {{0.12F, 0.18F, 0.26F, 1.0F}};
        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(render_target.view(), clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;

        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdEndRendering(command_buffer);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_readback_transition(render_target.handle()));
        commands.submit_and_wait();
    }

    cubey::vulkan::Instance& vulkan_instance() {
        if (!instance_.has_value()) {
            throw std::runtime_error("Vulkan instance is not initialized");
        }
        return instance_.value();
    }

    cubey::vulkan::Device& vulkan_device() {
        if (!device_.has_value()) {
            throw std::runtime_error("Vulkan device is not initialized");
        }
        return device_.value();
    }

    RunConfig config_;
    std::optional<cubey::vulkan::Instance> instance_;
    std::optional<cubey::vulkan::Device> device_;
};

} // namespace

int run_headless_render(const RunConfig& config) {
    HeadlessRenderApp app(config);
    return app.run();
}

} // namespace cubey::examples::headless_render
