#include <cubey/host/headless_png_host.h>

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/queue_submit.h>
#include <cubey/vulkan/vk_check.h>

#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::host {
namespace {

constexpr std::size_t kRgba8BytesPerPixel = 4;
constexpr std::size_t kVideoCaptureSlotCount = 3;

void validate_config(const HeadlessPngHostConfig& config,
                     const HeadlessPngHostCallbacks& callbacks) {
    if (config.run_config.width == 0 || config.run_config.height == 0) {
        throw std::runtime_error("headless PNG host dimensions must be positive");
    }
    if (config.required_queue_flags == 0) {
        throw std::runtime_error("headless PNG host requires at least one queue flag");
    }
    if (config.output_format != VK_FORMAT_R8G8B8A8_SRGB &&
        config.output_format != VK_FORMAT_R8G8B8A8_UNORM) {
        throw std::runtime_error("headless PNG host currently supports only RGBA8 output");
    }
    if (!callbacks.record_capture && !callbacks.record_frame) {
        throw std::runtime_error("headless PNG host requires a record callback");
    }
}

struct HeadlessCaptureSlot {
    HeadlessCaptureSlot(cubey::vulkan::Device& device, VkExtent2D extent, VkFormat format)
        : image(device, cubey::vulkan::color_render_target_image_config(extent, format)),
          readback(device, cubey::vulkan::readback_buffer_config(static_cast<VkDeviceSize>(
                               video_frame_byte_size(extent.width, extent.height)))),
          command_pool(device,
                       cubey::vulkan::CommandPoolConfig{
                           .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                       }),
          command_buffer(command_pool.allocate_primary()), device_handle(device.handle()),
          target{
              .extent = extent,
              .format = image.format(),
              .image = image.handle(),
              .view = image.view(),
          } {
        auto fence_info =
            cubey::vulkan::vk_struct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        cubey::vulkan::check(vkCreateFence(device_handle, &fence_info, nullptr, &fence),
                             "vkCreateFence headless video capture");
    }

    ~HeadlessCaptureSlot() {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device_handle, fence, nullptr);
        }
    }

    HeadlessCaptureSlot(const HeadlessCaptureSlot&) = delete;
    HeadlessCaptureSlot& operator=(const HeadlessCaptureSlot&) = delete;

    cubey::vulkan::Image image;
    cubey::vulkan::Buffer readback;
    cubey::vulkan::CommandPool command_pool;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkDevice device_handle = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    cubey::vulkan::GpuSubmissionTicket ticket{};
    HeadlessRenderTarget target{};
    bool submitted = false;
};

} // namespace

HeadlessPngContext::HeadlessPngContext(const RunConfig& config, cubey::vulkan::Instance& instance,
                                       cubey::vulkan::Device& device,
                                       cubey::vulkan::GpuRuntime& gpu,
                                       const HeadlessRenderTarget& target,
                                       cubey::profiling::ProfileRecorder* profile_recorder)
    : config_(config), instance_(instance), device_(device), gpu_(gpu), target_(target),
      profile_recorder_(profile_recorder) {}

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

std::uint32_t headless_capture_frame_slot_count(const RunConfig& config) {
    return config.capture_mode == CaptureMode::Video
               ? static_cast<std::uint32_t>(kVideoCaptureSlotCount)
               : cubey::render::kSingleFrameSlotCount;
}

std::uint32_t headless_capture_frame_count(const RunConfig& config) {
    if (config.capture_mode == CaptureMode::Video) {
        return config.frames == 0 ? 300 : config.frames;
    }
    return 1;
}

HeadlessCaptureFrame headless_capture_frame(const RunConfig& config, std::uint32_t frame_index) {
    const std::uint32_t count = headless_capture_frame_count(config);
    if (frame_index >= count) {
        throw std::runtime_error("headless capture frame index is out of range");
    }

    HeadlessCaptureFrame frame{
        .index = frame_index,
        .count = count,
        .frame_slot = cubey::render::frame_slot_for_index(
            frame_index, headless_capture_frame_slot_count(config)),
    };
    if (config.capture_mode == CaptureMode::Video) {
        if (config.fps == 0) {
            throw std::runtime_error("headless video capture fps must be positive");
        }
        const double fps = static_cast<double>(config.fps);
        frame.timing = FrameTiming{
            .delta_seconds = 1.0 / fps,
            .elapsed_seconds = static_cast<double>(frame_index) / fps,
            .frame_index = frame_index,
        };
    } else {
        frame.timing = FrameTiming{
            .delta_seconds = 0.0,
            .elapsed_seconds = 0.0,
            .frame_index = 0,
        };
    }
    return frame;
}

FrameTiming headless_video_simulation_timing(const HeadlessCaptureFrame& frame) {
    if (frame.timing.delta_seconds <= 0.0) {
        throw std::runtime_error("headless video simulation timing requires positive delta time");
    }
    const std::uint64_t simulation_frame = static_cast<std::uint64_t>(frame.index) + 1U;
    return {
        .delta_seconds = frame.timing.delta_seconds,
        .elapsed_seconds = frame.timing.delta_seconds * static_cast<double>(simulation_frame),
        .frame_index = simulation_frame,
    };
}

HeadlessCaptureFrame headless_simulation_frame(const RunConfig& config, std::uint32_t frame_index,
                                               std::uint32_t frame_count, FrameTiming timing) {
    if (frame_count == 0) {
        throw std::runtime_error("headless simulation requires at least one frame");
    }
    if (frame_index >= frame_count) {
        throw std::runtime_error("headless simulation frame index is out of range");
    }
    return {
        .index = frame_index,
        .count = frame_count,
        .frame_slot = cubey::render::frame_slot_for_index(
            frame_index, headless_capture_frame_slot_count(config)),
        .timing = timing,
    };
}

void install_headless_simulation_driver(HeadlessPngHostCallbacks& callbacks, RunConfig config,
                                        HeadlessSimulationDriver driver) {
    if (!driver.png_timing) {
        throw std::runtime_error("headless simulation driver requires PNG timing callback");
    }
    if (!driver.simulate_frame) {
        throw std::runtime_error("headless simulation driver requires simulate_frame callback");
    }
    if (driver.png_frame_count == 0) {
        throw std::runtime_error("headless simulation driver requires at least one PNG frame");
    }

    auto shared_driver = std::make_shared<HeadlessSimulationDriver>(std::move(driver));
    std::function<void(HeadlessPngContext&)> previous_before_capture =
        std::move(callbacks.before_capture);
    callbacks.before_capture = [config = std::move(config), shared_driver,
                                previous_before_capture = std::move(previous_before_capture)](
                                   HeadlessPngContext& context) mutable {
        if (previous_before_capture) {
            previous_before_capture(context);
        }
        if (config.capture_mode != CaptureMode::Png) {
            return;
        }
        for (std::uint32_t frame_index = 0; frame_index < shared_driver->png_frame_count;
             ++frame_index) {
            const std::uint64_t simulation_frame = static_cast<std::uint64_t>(frame_index) + 1U;
            const HeadlessCaptureFrame frame =
                headless_simulation_frame(config, frame_index, shared_driver->png_frame_count,
                                          shared_driver->png_timing(simulation_frame));
            shared_driver->simulate_frame(context, frame);
        }
    };

    std::function<void(HeadlessPngContext&, const HeadlessCaptureFrame&)> previous_before_frame =
        std::move(callbacks.before_frame);
    callbacks.before_frame =
        [shared_driver, previous_before_frame = std::move(previous_before_frame)](
            HeadlessPngContext& context, const HeadlessCaptureFrame& frame) mutable {
            if (previous_before_frame) {
                previous_before_frame(context, frame);
            }
            if (context.config().capture_mode != CaptureMode::Video) {
                return;
            }
            HeadlessCaptureFrame simulation_frame = frame;
            simulation_frame.timing = headless_video_simulation_timing(frame);
            shared_driver->simulate_frame(context, simulation_frame);
        };
}

HeadlessPngHost::HeadlessPngHost(HeadlessPngHostConfig config, HeadlessPngHostCallbacks callbacks)
    : config_(std::move(config)), callbacks_(std::move(callbacks)), encoding_jobs_(1),
      captures_(encoding_jobs_) {
    validate_config(config_, callbacks_);
    create_profile_recorder();
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
    create_project_gpu_services();

    const VkExtent2D extent{config_.run_config.width, config_.run_config.height};
    cubey::vulkan::Image render_target_image(
        device(), cubey::vulkan::color_render_target_image_config(extent, config_.output_format));
    const HeadlessRenderTarget target{
        .extent = extent,
        .format = render_target_image.format(),
        .image = render_target_image.handle(),
        .view = render_target_image.view(),
    };
    HeadlessPngContext context(config_.run_config, instance(), device(), gpu(), target,
                               profile_recorder());

    try {
        if (callbacks_.create_resources) {
            [[maybe_unused]] auto span = profile_span(0, "headless.create_resources");
            callbacks_.create_resources(context);
        }
        drain_gpu_work();
        if (callbacks_.before_capture) {
            [[maybe_unused]] auto span = profile_span(0, "headless.before_capture");
            callbacks_.before_capture(context);
        }
        drain_gpu_work();

        if (config_.run_config.capture_mode == CaptureMode::Video) {
            [[maybe_unused]] auto span = profile_span(0, "headless.write_video");
            write_video(context, target);
        } else {
            const HeadlessCaptureFrame frame = headless_capture_frame(config_.run_config, 0);
            if (callbacks_.before_frame) {
                [[maybe_unused]] auto span = profile_span(frame.index, "headless.before_frame");
                callbacks_.before_frame(context, frame);
                drain_gpu_work();
            }
            record_capture(context, target, frame);
            record_profile_frame(frame, target);
            {
                [[maybe_unused]] auto span = profile_span(frame.index, "headless.write_png");
                write_png(target);
            }
        }
        device().wait_idle();

        drain_gpu_work();
        shutdown_resources(context);
        write_profile_outputs();
        return 0;
    } catch (...) {
        try {
            device().wait_idle();
            drain_gpu_work();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "headless PNG host idle failed during cleanup: %s\n",
                         error.what());
        } catch (...) {
            std::fprintf(stderr, "headless PNG host idle failed during cleanup\n");
        }
        try {
            shutdown_resources(context);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "headless PNG host shutdown failed: %s\n", error.what());
        } catch (...) {
            std::fprintf(stderr, "headless PNG host shutdown failed\n");
        }
        throw;
    }
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
    device_config.require_tessellation_shader = config_.require_tessellation_shader;
    device_.emplace(instance(), device_config);
}

void HeadlessPngHost::create_submission_coordinator() {
    submission_.emplace(device());
}

void HeadlessPngHost::create_gpu_runtime() {
    gpu_.emplace(cubey::vulkan::GpuRuntimeConfig{
        .device = &device(),
        .submission = &submission(),
        .execution_mode = config_.gpu_execution_mode,
    });
}

void HeadlessPngHost::create_project_gpu_services() {
    project_gpu_.emplace(gpu(), uploads_, deferred_destruction_);
}

void HeadlessPngHost::create_profile_recorder() {
    if (config_.run_config.profile_output_prefix.empty()) {
        return;
    }
    profile_recorder_.emplace(cubey::profiling::ProfileRecorderConfig{
        .output_prefix = config_.run_config.profile_output_prefix,
        .warmup_frames = config_.run_config.profile_warmup_frames,
    });
}

void HeadlessPngHost::write_profile_outputs() {
    if (!profile_recorder_.has_value()) {
        return;
    }
    profile_recorder_->write_outputs();
    const std::string prefix = profile_recorder_->config().output_prefix.string();
    std::printf("profile: wrote %s.{frames.csv,passes.csv,metrics.csv,trace.json,summary.txt}\n",
                prefix.c_str());
}

void HeadlessPngHost::drain_gpu_work() {
    static_cast<void>(gpu().drain());
}

cubey::profiling::ProfileRecorder* HeadlessPngHost::profile_recorder() {
    return profile_recorder_.has_value() ? &profile_recorder_.value() : nullptr;
}

cubey::profiling::ScopedCpuProfileSpan HeadlessPngHost::profile_span(std::uint64_t frame_index,
                                                                     std::string_view label) {
    cubey::profiling::ProfileRecorder* recorder = profile_recorder();
    if (recorder == nullptr) {
        return {};
    }
    return recorder->cpu_span(frame_index, label);
}

void HeadlessPngHost::record_profile_frame(const HeadlessCaptureFrame& frame,
                                           const HeadlessRenderTarget& target) {
    cubey::profiling::ProfileRecorder* recorder = profile_recorder();
    if (recorder == nullptr) {
        return;
    }
    const cubey::vulkan::DeviceMemoryBudgetInfo memory = device().device_memory_budget();
    recorder->record_frame({
        .frame_index = frame.index,
        .delta_seconds = frame.timing.delta_seconds,
        .width = target.extent.width,
        .height = target.extent.height,
        .triangles = 0,
        .memory_budget_available = memory.available,
        .device_local_usage = memory.device_local_usage,
        .device_local_budget = memory.device_local_budget,
        .device_local_heap_size = memory.device_local_heap_size,
    });
}

void HeadlessPngHost::record_capture(HeadlessPngContext& context,
                                     const HeadlessRenderTarget& target,
                                     const HeadlessCaptureFrame& frame) {
    [[maybe_unused]] auto span = profile_span(frame.index, "headless.record_capture");
    static_cast<void>(gpu().enqueue(cubey::vulkan::GpuWorkRequest{
        .label = "headless PNG capture",
        .work =
            [this, &context, target, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                cubey::vulkan::ImmediateCommands commands(gpu_context.device(),
                                                          gpu_context.submission());
                const VkCommandBuffer command_buffer = commands.command_buffer();
                cubey::vulkan::transition_image_layout(
                    command_buffer, cubey::vulkan::begin_color_attachment_transition(target.image));
                if (callbacks_.record_frame) {
                    callbacks_.record_frame(context, frame, command_buffer, target);
                } else {
                    callbacks_.record_capture(context, command_buffer, target);
                }
                cubey::vulkan::transition_image_layout(
                    command_buffer,
                    cubey::vulkan::finish_color_attachment_for_readback_transition(target.image));
                commands.submit_and_wait();
            },
    }));
    drain_gpu_work();
}

ProjectGpuReadbackResult HeadlessPngHost::readback_target(const HeadlessRenderTarget& target,
                                                          const char* label) {
    const ProjectGpuReadbackTicket ticket =
        project_gpu().enqueue_rgba8_image_readback(target.image, target.extent, label);
    static_cast<void>(project_gpu().drain());
    return project_gpu().take_completed_readback(ticket);
}

void HeadlessPngHost::write_png(const HeadlessRenderTarget& target) {
    ProjectGpuReadbackResult readback = readback_target(target, "headless PNG RGBA8 readback");
    CaptureTicket ticket = captures_.enqueue_png({
        .output_path = config_.run_config.output_path,
        .width = readback.width,
        .height = readback.height,
        .rgba8 = std::move(readback.rgba8),
    });
    ticket.finish();

    const std::string output_path = config_.run_config.output_path.string();
    std::printf("headless_png: %s wrote %s at %ux%u\n", device().device_name(), output_path.c_str(),
                target.extent.width, target.extent.height);
}

void HeadlessPngHost::write_video(HeadlessPngContext& context, const HeadlessRenderTarget& target) {
    if (!video_encoding_available()) {
        throw std::runtime_error(
            "video capture requested, but no libav H.264 backend is available");
    }
    (void)context;

    QueuedVideoEncoder encoder = captures_.start_video_encoding({
        .output_path = config_.run_config.output_path,
        .width = target.extent.width,
        .height = target.extent.height,
        .fps = config_.run_config.fps,
    });

    const std::uint32_t slot_count = headless_capture_frame_slot_count(config_.run_config);
    std::vector<std::unique_ptr<HeadlessCaptureSlot>> slots;
    slots.reserve(slot_count);
    for (std::uint32_t index = 0; index < slot_count; ++index) {
        slots.push_back(
            std::make_unique<HeadlessCaptureSlot>(device(), target.extent, target.format));
    }

    auto wait_for_slot = [this, &encoder](HeadlessCaptureSlot& slot) {
        if (!slot.submitted) {
            return;
        }
        cubey::vulkan::check(
            vkWaitForFences(device().handle(), 1, &slot.fence, VK_TRUE, UINT64_MAX),
            "vkWaitForFences headless video capture");
        gpu().mark_submission_completed(slot.ticket);

        VideoFrameEncodeRequest frame{
            .width = slot.target.extent.width,
            .height = slot.target.extent.height,
            .rgba8 = std::vector<std::uint8_t>(
                video_frame_byte_size(slot.target.extent.width, slot.target.extent.height)),
        };
        slot.readback.download(frame.rgba8.data(), static_cast<VkDeviceSize>(frame.rgba8.size()));
        slot.submitted = false;
        encoder.enqueue_frame(std::move(frame));
    };

    auto submit_slot = [this](HeadlessPngContext& frame_context, HeadlessCaptureSlot& slot,
                              HeadlessCaptureFrame frame) {
        [[maybe_unused]] auto span = profile_span(frame.index, "headless.submit_video_frame");
        static_cast<void>(gpu().enqueue(cubey::vulkan::GpuWorkRequest{
            .label = "headless video capture",
            .work =
                [this, &frame_context, &slot, frame](cubey::vulkan::GpuOwnerContext& gpu_context) {
                    cubey::vulkan::check(
                        vkResetFences(gpu_context.device().handle(), 1, &slot.fence),
                        "vkResetFences headless video capture");
                    cubey::vulkan::check(vkResetCommandBuffer(slot.command_buffer, 0),
                                         "vkResetCommandBuffer headless video capture");
                    cubey::vulkan::begin_command_buffer(
                        slot.command_buffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
                    cubey::vulkan::transition_image_layout(
                        slot.command_buffer,
                        cubey::vulkan::begin_color_attachment_transition(slot.target.image));
                    if (callbacks_.record_frame) {
                        callbacks_.record_frame(frame_context, frame, slot.command_buffer,
                                                slot.target);
                    } else {
                        callbacks_.record_capture(frame_context, slot.command_buffer, slot.target);
                    }
                    cubey::vulkan::transition_image_layout(
                        slot.command_buffer,
                        cubey::vulkan::finish_color_attachment_for_readback_transition(
                            slot.target.image));
                    const VkBufferImageCopy copy = cubey::vulkan::buffer_image_copy(
                        VkExtent3D{slot.target.extent.width, slot.target.extent.height, 1});
                    vkCmdCopyImageToBuffer(slot.command_buffer, slot.target.image,
                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           slot.readback.handle(), 1, &copy);
                    cubey::vulkan::end_command_buffer(slot.command_buffer,
                                                      "vkEndCommandBuffer headless video capture");
                    slot.ticket = gpu_context.submission().submit(
                        {
                            .command_buffers = {slot.command_buffer},
                            .fence = slot.fence,
                        },
                        "vkQueueSubmit headless video capture");
                    slot.submitted = true;
                },
        }));
        drain_gpu_work();
    };

    const std::uint32_t frame_count = headless_capture_frame_count(config_.run_config);
    for (std::uint32_t index = 0; index < frame_count; ++index) {
        HeadlessCaptureSlot& slot = *slots[index % slots.size()];
        wait_for_slot(slot);

        const HeadlessCaptureFrame frame = headless_capture_frame(config_.run_config, index);
        HeadlessPngContext frame_context(config_.run_config, instance(), device(), gpu(),
                                         slot.target, profile_recorder());
        if (callbacks_.before_frame) {
            [[maybe_unused]] auto span = profile_span(frame.index, "headless.before_frame");
            callbacks_.before_frame(frame_context, frame);
            drain_gpu_work();
        }
        submit_slot(frame_context, slot, frame);
        record_profile_frame(frame, slot.target);
    }
    for (const std::unique_ptr<HeadlessCaptureSlot>& slot : slots) {
        wait_for_slot(*slot);
    }
    encoder.finish();

    const std::string output_path = config_.run_config.output_path.string();
    std::printf("headless_video: %s wrote %s at %ux%u, %u frames @ %u fps via %s\n",
                device().device_name(), output_path.c_str(), target.extent.width,
                target.extent.height, frame_count, config_.run_config.fps,
                video_encoding_backend_name());
}

void HeadlessPngHost::shutdown_resources(HeadlessPngContext& context) {
    if (shutdown_called_ || !callbacks_.shutdown) {
        return;
    }

    shutdown_called_ = true;
    [[maybe_unused]] auto span = profile_span(0, "headless.shutdown");
    callbacks_.shutdown(context);
    drain_gpu_work();
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

ProjectGpuServices& HeadlessPngHost::project_gpu() {
    if (!project_gpu_.has_value()) {
        throw std::runtime_error("headless PNG project GPU services are not initialized");
    }
    return project_gpu_.value();
}

} // namespace cubey::host
