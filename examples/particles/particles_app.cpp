#include "particles_app.h"

#include <cubey/frame_clock.h>
#include <cubey/frame_stats.h>
#include <cubey/spirv_io.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/frame_resources.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/render_context.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/swapchain.h>
#include <cubey/vulkan/vk_check.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CUBEY_PARTICLES_SHADER_DIR
#error "CUBEY_PARTICLES_SHADER_DIR must be defined by the particles CMake target"
#endif

namespace cubey::examples::particles {
namespace {

using cubey::format_window_title;
using cubey::FrameClock;
using cubey::FrameStats;
using cubey::FrameStatsSnapshot;
using cubey::FrameTiming;
using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

constexpr std::uint32_t kParticleCount = 8192;
constexpr float kGoldenAngle = 2.39996314F;

struct ParticleGpu {
    std::array<float, 4> position_radius{};
    std::array<float, 4> velocity_seed{};
    std::array<float, 4> color{};
};

struct DrawPushConstants {
    std::array<float, 4> inv_extent_scale_time{};
};

static_assert(sizeof(ParticleGpu) == 48);
static_assert(sizeof(DrawPushConstants) == 16);

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PARTICLES_SHADER_DIR) / filename;
}

[[nodiscard]] float hash01(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
}

[[nodiscard]] std::vector<ParticleGpu> make_initial_particles() {
    std::vector<ParticleGpu> particles;
    particles.reserve(kParticleCount);

    for (std::uint32_t i = 0; i < kParticleCount; ++i) {
        const float rank = (static_cast<float>(i) + 0.5F) / static_cast<float>(kParticleCount);
        const float angle = static_cast<float>(i) * kGoldenAngle;
        const float radius = std::sqrt(rank) * 0.84F;
        const float jitter = (hash01(i * 747796405U + 2891336453U) - 0.5F) * 0.08F;
        const float x = std::cos(angle) * (radius + jitter);
        const float y = std::sin(angle) * (radius + jitter);
        const float heat = hash01(i * 277803737U + 1013904223U);
        const float size_pixels = 2.0F + hash01(i * 1597334677U + 3812015801U) * 4.5F;

        particles.push_back({
            .position_radius = {x, y, 0.0F, size_pixels},
            .velocity_seed = {-y * 0.08F, x * 0.08F, 0.0F, heat},
            .color =
                {
                    0.25F + heat * 0.70F,
                    0.55F + (1.0F - heat) * 0.25F,
                    0.95F - heat * 0.40F,
                    0.24F,
                },
        });
    }

    return particles;
}

class ParticlesApp {
  public:
    explicit ParticlesApp(RunConfig config) : config_(std::move(config)) {}

    ParticlesApp(const ParticlesApp&) = delete;
    ParticlesApp& operator=(const ParticlesApp&) = delete;

    ~ParticlesApp() {
        if (device_ != VK_NULL_HANDLE) {
            static_cast<void>(vkDeviceWaitIdle(device_));
        }

        frame_resources_.reset();
        destroy_swapchain_resources();
        descriptor_pool_.reset();
        descriptor_set_layout_.reset();
        particle_buffer_.reset();

        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        device_owner_.reset();
        device_ = VK_NULL_HANDLE;
        instance_owner_.reset();
        instance_ = VK_NULL_HANDLE;
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
        if (glfw_initialized_) {
            glfwTerminate();
        }
    }

    int run() {
        if (config_.headless) {
            throw std::runtime_error("particles does not support --headless yet");
        }

        init_window();
        create_instance();
        create_surface();
        create_device();
        create_particle_buffer();
        create_descriptor_resources();
        create_swapchain_resources();
        create_frame_resources();
        render_window();
        return 0;
    }

  private:
    void init_window() {
        if (glfwInit() == 0) {
            throw std::runtime_error("glfwInit failed");
        }
        glfw_initialized_ = true;

        if (glfwVulkanSupported() == 0) {
            throw std::runtime_error("GLFW reports Vulkan is not supported");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ =
            glfwCreateWindow(static_cast<int>(config_.width), static_cast<int>(config_.height),
                             config_.title.c_str(), nullptr, nullptr);
        if (window_ == nullptr) {
            throw std::runtime_error("glfwCreateWindow failed");
        }

        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    }

    // GLFW fixes this callback signature.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static void framebuffer_size_callback(GLFWwindow* window, int unused_width, int unused_height) {
        (void)unused_width;
        (void)unused_height;
        auto* app = static_cast<ParticlesApp*>(glfwGetWindowUserPointer(window));
        if (app != nullptr) {
            app->framebuffer_resized_ = true;
        }
    }

    void create_instance() {
        std::uint32_t extension_count = 0;
        const char** required_extensions = glfwGetRequiredInstanceExtensions(&extension_count);
        if (required_extensions == nullptr || extension_count == 0) {
            throw std::runtime_error("glfwGetRequiredInstanceExtensions failed");
        }

        cubey::vulkan::InstanceConfig instance_config;
        instance_config.application_name = config_.title;
        instance_config.required_extensions.assign(required_extensions,
                                                   required_extensions + extension_count);
        instance_config.validation = config_.validation;
        instance_config.require_validation = config_.require_validation;

        instance_owner_.emplace(instance_config);
        instance_ = instance_owner_->handle();
    }

    void create_surface() {
        check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
              "glfwCreateWindowSurface");
    }

    void create_device() {
        cubey::vulkan::DeviceConfig device_config;
        device_config.surface = surface_;
        device_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
        device_config.require_present = true;
        device_config.require_dynamic_rendering = true;

        if (!instance_owner_.has_value()) {
            throw std::runtime_error("Vulkan instance must exist before creating a device");
        }
        device_owner_.emplace(instance_owner_.value(), device_config);
        device_ = device_owner_->handle();
    }

    void create_particle_buffer() {
        const std::vector<ParticleGpu> particles = make_initial_particles();
        const VkDeviceSize byte_size =
            static_cast<VkDeviceSize>(particles.size() * sizeof(ParticleGpu));
        particle_buffer_.emplace(cubey::vulkan::upload_device_buffer(
            vulkan_device(), particles.data(), byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    }

    void create_descriptor_resources() {
        const VkDescriptorSetLayoutBinding particle_binding = cubey::vulkan::descriptor_binding(
            0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
        const std::array<VkDescriptorSetLayoutBinding, 1> bindings{particle_binding};
        const VkDescriptorSetLayoutCreateInfo layout_info =
            cubey::vulkan::descriptor_set_layout_info(bindings);
        descriptor_set_layout_.emplace(vulkan_device(), layout_info);

        const VkDescriptorPoolSize pool_size =
            cubey::vulkan::descriptor_pool_size(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
        const std::array<VkDescriptorPoolSize, 1> pool_sizes{pool_size};
        const VkDescriptorPoolCreateInfo pool_info =
            cubey::vulkan::descriptor_pool_info(1, pool_sizes);
        descriptor_pool_.emplace(vulkan_device(), pool_info);
        descriptor_set_ = descriptor_pool().allocate(descriptor_set_layout().handle());
        update_particle_descriptor();
    }

    void update_particle_descriptor() {
        const cubey::vulkan::DescriptorBufferWrite particle_write =
            cubey::vulkan::storage_buffer_descriptor(descriptor_set_, 0, particle_buffer().handle(),
                                                     particle_buffer().size());
        const VkWriteDescriptorSet write = particle_write.descriptor_write();
        cubey::vulkan::update_descriptor_sets(vulkan_device(), {&write, 1});
    }

    void wait_for_presentable_window_size() const {
        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        while ((fb_width == 0 || fb_height == 0) && glfwWindowShouldClose(window_) == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        }
        if (fb_width == 0 || fb_height == 0) {
            throw std::runtime_error(
                "window closed before a presentable framebuffer size was available");
        }
    }

    [[nodiscard]] VkExtent2D current_framebuffer_extent() const {
        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        return {
            static_cast<std::uint32_t>(fb_width),
            static_cast<std::uint32_t>(fb_height),
        };
    }

    void create_swapchain_resources() {
        create_swapchain();
        create_pipeline();
    }

    void destroy_swapchain_resources() {
        pipeline_.reset();
        pipeline_layout_.reset();
        swapchain_.reset();
    }

    void recreate_swapchain_resources() {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle before swapchain recreate");
        frame_resources_.reset();
        destroy_swapchain_resources();
        create_swapchain_resources();
        create_frame_resources();
    }

    void create_swapchain() {
        wait_for_presentable_window_size();

        cubey::vulkan::SwapchainConfig swapchain_config;
        swapchain_config.surface = surface_;
        swapchain_config.desired_extent = current_framebuffer_extent();
        swapchain_config.image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_.emplace(vulkan_device(), swapchain_config);
        framebuffer_resized_ = false;
    }

    void create_pipeline() {
        const std::vector<std::uint32_t> vertex_code =
            cubey::read_spirv_file(shader_path("particles.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::read_spirv_file(shader_path("particles.frag.spv"));
        cubey::vulkan::ShaderModule vertex_shader(vulkan_device(), vertex_code);
        cubey::vulkan::ShaderModule fragment_shader(vulkan_device(), fragment_code);

        const VkPipelineShaderStageCreateInfo vertex_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader.handle());
        const VkPipelineShaderStageCreateInfo fragment_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader.handle());

        const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
            vertex_stage,
            fragment_stage,
        };

        VkPushConstantRange draw_push_constant{};
        draw_push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        draw_push_constant.offset = 0;
        draw_push_constant.size = sizeof(DrawPushConstants);
        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptor_set_layout().handle()};
        const std::array<VkPushConstantRange, 1> push_constants{draw_push_constant};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = push_constants,
        });
        pipeline_layout_.emplace(vulkan_device(), layout_info.create_info());

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = pipeline_layout().handle();
        pipeline_config.extent = swapchain().extent();
        pipeline_config.color_format = swapchain().format();
        pipeline_config.shader_stages = shader_stages;
        pipeline_config.blend_enable = true;
        pipeline_config.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
        pipeline_config.dst_color_blend_factor = VK_BLEND_FACTOR_ONE;
        pipeline_config.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
        pipeline_config.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        pipeline_.emplace(vulkan_device(), pipeline_info.create_info());
    }

    void create_frame_resources() {
        frame_resources_.emplace(vulkan_device(), swapchain().image_count());
    }

    void record_particles_frame(VkCommandBuffer command_buffer, std::uint32_t image_index,
                                const FrameTiming& timing) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain().images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));

        VkClearValue clear{};
        clear.color = {{0.006F, 0.007F, 0.012F, 1.0F}};
        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(
                swapchain().image_views().at(swapchain_image_index), clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = swapchain().extent();
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;

        const VkExtent2D extent = swapchain().extent();
        const DrawPushConstants push_constants{
            .inv_extent_scale_time =
                {
                    1.0F / static_cast<float>(extent.width),
                    1.0F / static_cast<float>(extent.height),
                    1.0F,
                    static_cast<float>(timing.elapsed_seconds),
                },
        };

        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline().handle());
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout().handle(), 0, 1, &descriptor_set_, 0, nullptr);
        vkCmdPushConstants(command_buffer, pipeline_layout().handle(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(DrawPushConstants), &push_constants);
        vkCmdDraw(command_buffer, 6, kParticleCount, 0, 0);
        vkCmdEndRendering(command_buffer);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_present_transition(swapchain_image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer particles");
    }

    cubey::vulkan::RenderFrameResult draw_frame(const FrameTiming& timing) {
        cubey::vulkan::RenderContext render_context({
            .device = &vulkan_device(),
            .swapchain = &swapchain(),
            .frame_resources = &frame_resources(),
        });

        cubey::vulkan::RenderFrame frame;
        cubey::vulkan::RenderFrameResult result = render_context.begin_frame(&frame);
        if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
            return result;
        }

        record_particles_frame(frame.command_buffer, frame.image_index, timing);
        return render_context.end_frame(frame);
    }

    void reset_frame_timing() {
        frame_clock_.reset();
        frame_stats_.reset();
    }

    void render_window() {
        reset_frame_timing();

        std::printf("particles: %s rendering instanced particle billboards at %ux%u\n",
                    vulkan_device().device_name(), swapchain().extent().width,
                    swapchain().extent().height);

        std::uint32_t frame = 0;
        cubey::vulkan::SwapchainRecreateTracker recreate_tracker;
        while (glfwWindowShouldClose(window_) == 0 &&
               (config_.frames == 0 || frame < config_.frames)) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window_) != 0) {
                break;
            }

            if (framebuffer_resized_) {
                std::puts("framebuffer resized; recreating swapchain");
                recreate_swapchain_resources();
                reset_frame_timing();
                recreate_tracker.reset();
                continue;
            }

            const FrameTiming timing = frame_clock_.tick();
            cubey::vulkan::RenderFrameResult result = draw_frame(timing);
            if (result == cubey::vulkan::RenderFrameResult::RecreateSwapchain) {
                recreate_tracker.record_recreate_request();
                std::puts("swapchain out of date; recreating");
                recreate_swapchain_resources();
                reset_frame_timing();
                continue;
            }

            recreate_tracker.reset();
            const VkExtent2D extent = swapchain().extent();
            std::optional<FrameStatsSnapshot> stats = frame_stats_.record_frame({
                .delta_seconds = timing.delta_seconds,
                .width = extent.width,
                .height = extent.height,
                .triangles = kParticleCount * 2U,
            });
            if (stats.has_value()) {
                const std::string title = format_window_title(config_.title, stats.value());
                glfwSetWindowTitle(window_, title.c_str());
            }
            ++frame;
        }

        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle after particles");
    }

    [[nodiscard]] cubey::vulkan::Swapchain& swapchain() {
        if (!swapchain_.has_value()) {
            throw std::runtime_error("swapchain is not initialized");
        }
        return swapchain_.value();
    }

    [[nodiscard]] const cubey::vulkan::Swapchain& swapchain() const {
        if (!swapchain_.has_value()) {
            throw std::runtime_error("swapchain is not initialized");
        }
        return swapchain_.value();
    }

    [[nodiscard]] cubey::vulkan::Device& vulkan_device() {
        if (!device_owner_.has_value()) {
            throw std::runtime_error("Vulkan device is not initialized");
        }
        return device_owner_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetLayout& descriptor_set_layout() const {
        if (!descriptor_set_layout_.has_value()) {
            throw std::runtime_error("particle descriptor set layout is not initialized");
        }
        return descriptor_set_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorPool& descriptor_pool() const {
        if (!descriptor_pool_.has_value()) {
            throw std::runtime_error("particle descriptor pool is not initialized");
        }
        return descriptor_pool_.value();
    }

    [[nodiscard]] const cubey::vulkan::Buffer& particle_buffer() const {
        if (!particle_buffer_.has_value()) {
            throw std::runtime_error("particle buffer is not initialized");
        }
        return particle_buffer_.value();
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& pipeline_layout() const {
        if (!pipeline_layout_.has_value()) {
            throw std::runtime_error("pipeline layout is not initialized");
        }
        return pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::GraphicsPipeline& pipeline() const {
        if (!pipeline_.has_value()) {
            throw std::runtime_error("pipeline is not initialized");
        }
        return pipeline_.value();
    }

    [[nodiscard]] cubey::vulkan::FrameResources& frame_resources() {
        if (!frame_resources_.has_value()) {
            throw std::runtime_error("frame resources are not initialized");
        }
        return frame_resources_.value();
    }

    RunConfig config_;
    bool glfw_initialized_ = false;
    bool framebuffer_resized_ = false;
    GLFWwindow* window_ = nullptr;

    std::optional<cubey::vulkan::Instance> instance_owner_;
    std::optional<cubey::vulkan::Device> device_owner_;
    std::optional<cubey::vulkan::Buffer> particle_buffer_;
    std::optional<cubey::vulkan::DescriptorSetLayout> descriptor_set_layout_;
    std::optional<cubey::vulkan::DescriptorPool> descriptor_pool_;
    std::optional<cubey::vulkan::Swapchain> swapchain_;
    std::optional<cubey::vulkan::FrameResources> frame_resources_;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    std::optional<cubey::vulkan::PipelineLayout> pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> pipeline_;
    FrameClock frame_clock_;
    FrameStats frame_stats_;
};

} // namespace

int run_particles(const RunConfig& config) {
    ParticlesApp app(config);
    return app.run();
}

} // namespace cubey::examples::particles
