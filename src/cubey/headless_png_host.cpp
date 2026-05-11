#include <cubey/headless_png_host.h>

#include <cubey/image_io.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/vk_check.h>

#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey {
namespace {

constexpr std::size_t kRgba8BytesPerPixel = 4;

void validate_config(const HeadlessPngHostConfig& config,
                     const HeadlessPngHostCallbacks& callbacks) {
    if (config.run_config.width == 0 || config.run_config.height == 0) {
        throw std::runtime_error("headless PNG host dimensions must be positive");
    }
    if (config.required_queue_flags == 0) {
        throw std::runtime_error("headless PNG host requires at least one queue flag");
    }
    if (config.output_format != VK_FORMAT_R8G8B8A8_UNORM) {
        throw std::runtime_error("headless PNG host currently supports only RGBA8 output");
    }
    if (!callbacks.record_capture) {
        throw std::runtime_error("headless PNG host requires a record_capture callback");
    }
}

} // namespace

HeadlessPngContext::HeadlessPngContext(const RunConfig& config, cubey::vulkan::Instance& instance,
                                       cubey::vulkan::Device& device,
                                       cubey::vulkan::SubmissionCoordinator& submission,
                                       cubey::vulkan::GpuRuntime& gpu,
                                       const HeadlessRenderTarget& target)
    : config_(config), instance_(instance), device_(device), submission_(submission), gpu_(gpu),
      target_(target) {}

std::size_t headless_png_byte_size(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        throw std::runtime_error("headless PNG dimensions must be positive");
    }

    const std::size_t checked_width = static_cast<std::size_t>(width);
    const std::size_t checked_height = static_cast<std::size_t>(height);
    if (checked_width > std::numeric_limits<std::size_t>::max() / checked_height) {
        throw std::runtime_error("headless PNG output is too large");
    }

    const std::size_t pixel_count = checked_width * checked_height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / kRgba8BytesPerPixel) {
        throw std::runtime_error("headless PNG output is too large");
    }
    return pixel_count * kRgba8BytesPerPixel;
}

HeadlessPngHost::HeadlessPngHost(HeadlessPngHostConfig config, HeadlessPngHostCallbacks callbacks)
    : config_(std::move(config)), callbacks_(std::move(callbacks)) {
    validate_config(config_, callbacks_);
}

HeadlessPngHost::~HeadlessPngHost() {
    if (device_.has_value()) {
        static_cast<void>(vkDeviceWaitIdle(device_->handle()));
    }
}

int HeadlessPngHost::run() {
    create_instance();
    create_device();
    create_submission_coordinator();
    create_gpu_runtime();

    const VkExtent2D extent{config_.run_config.width, config_.run_config.height};
    cubey::vulkan::Image render_target_image(
        device(), cubey::vulkan::color_render_target_image_config(extent, config_.output_format));
    const HeadlessRenderTarget target{
        .extent = extent,
        .format = render_target_image.format(),
        .image = render_target_image.handle(),
        .view = render_target_image.view(),
    };
    HeadlessPngContext context(config_.run_config, instance(), device(), submission(), gpu(),
                               target);

    if (callbacks_.create_resources) {
        callbacks_.create_resources(context);
    }
    drain_gpu_work();
    if (callbacks_.before_capture) {
        callbacks_.before_capture(context);
    }
    drain_gpu_work();

    record_capture(context, target);
    write_png(target);
    device().wait_idle();

    drain_gpu_work();
    if (callbacks_.shutdown) {
        callbacks_.shutdown(context);
        drain_gpu_work();
    }
    return 0;
}

void HeadlessPngHost::create_instance() {
    cubey::vulkan::InstanceConfig instance_config;
    instance_config.application_name = config_.run_config.title;
    instance_config.validation = config_.run_config.validation;
    instance_config.require_validation = config_.run_config.require_validation;
    instance_.emplace(instance_config);
}

void HeadlessPngHost::create_device() {
    cubey::vulkan::DeviceConfig device_config;
    device_config.required_queue_flags = config_.required_queue_flags;
    device_config.require_present = false;
    device_config.require_dynamic_rendering = config_.require_dynamic_rendering;
    device_.emplace(instance(), device_config);
}

void HeadlessPngHost::create_submission_coordinator() {
    submission_.emplace(device());
}

void HeadlessPngHost::create_gpu_runtime() {
    gpu_.emplace(device(), submission());
}

void HeadlessPngHost::drain_gpu_work() {
    static_cast<void>(gpu().drain_inline());
}

void HeadlessPngHost::record_capture(HeadlessPngContext& context,
                                     const HeadlessRenderTarget& target) {
    cubey::vulkan::ImmediateCommands commands(device());
    const VkCommandBuffer command_buffer = commands.command_buffer();
    cubey::vulkan::transition_image_layout(
        command_buffer, cubey::vulkan::begin_color_attachment_transition(target.image));
    callbacks_.record_capture(context, command_buffer, target);
    cubey::vulkan::transition_image_layout(
        command_buffer,
        cubey::vulkan::finish_color_attachment_for_readback_transition(target.image));
    commands.submit_and_wait();
}

void HeadlessPngHost::write_png(const HeadlessRenderTarget& target) {
    const std::size_t byte_size = headless_png_byte_size(target.extent.width, target.extent.height);
    const VkDeviceSize readback_byte_size = static_cast<VkDeviceSize>(byte_size);
    cubey::vulkan::Buffer readback(device(),
                                   cubey::vulkan::readback_buffer_config(readback_byte_size));
    cubey::vulkan::copy_image_to_buffer(device(), target.image, readback.handle(),
                                        {target.extent.width, target.extent.height, 1});

    std::vector<std::uint8_t> pixels(byte_size);
    readback.download(pixels.data(), readback_byte_size);
    cubey::write_png_rgba8(config_.run_config.output_path, target.extent.width,
                           target.extent.height, pixels);

    const std::string output_path = config_.run_config.output_path.string();
    std::printf("headless_png: %s wrote %s at %ux%u\n", device().device_name(), output_path.c_str(),
                target.extent.width, target.extent.height);
}

cubey::vulkan::Instance& HeadlessPngHost::instance() {
    if (!instance_.has_value()) {
        throw std::runtime_error("headless PNG Vulkan instance is not initialized");
    }
    return instance_.value();
}

cubey::vulkan::Device& HeadlessPngHost::device() {
    if (!device_.has_value()) {
        throw std::runtime_error("headless PNG Vulkan device is not initialized");
    }
    return device_.value();
}

cubey::vulkan::SubmissionCoordinator& HeadlessPngHost::submission() {
    if (!submission_.has_value()) {
        throw std::runtime_error("headless PNG Vulkan submission coordinator is not initialized");
    }
    return submission_.value();
}

cubey::vulkan::GpuRuntime& HeadlessPngHost::gpu() {
    if (!gpu_.has_value()) {
        throw std::runtime_error("headless PNG Vulkan GPU runtime is not initialized");
    }
    return gpu_.value();
}

} // namespace cubey
