#include "fluid_2d_app.h"

#include <cubey/app/glfw_window.h>
#include <cubey/app/windowed_host.h>
#include <cubey/frame_stats.h>
#include <cubey/spirv_io.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "fluid_2d_config.h"

#ifndef CUBEY_FLUID_2D_SHADER_DIR
#error "CUBEY_FLUID_2D_SHADER_DIR must be defined by the fluid_2d CMake target"
#endif

namespace cubey::projects::fluid_2d {
namespace {

using cubey::FrameStatsSample;
using cubey::FrameTiming;
using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

constexpr float kFallbackInjectionRadius = 0.08F;

struct RenderPushConstants {
    std::array<float, 4> grid_time{};
};

struct SimulationPushConstants {
    std::array<float, 4> grid_dt_time{};
    std::array<float, 4> injection_xy_radius_strength{};
    std::array<float, 4> injection_dye_active{};
    std::array<float, 4> force_decay{};
};

static_assert(sizeof(RenderPushConstants) == sizeof(float) * 4U);
static_assert(sizeof(SimulationPushConstants) == sizeof(float) * 16U);

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_FLUID_2D_SHADER_DIR) / filename;
}

class Fluid2DApp {
  public:
    explicit Fluid2DApp(RunConfig config) : config_(std::move(config)) {}

    Fluid2DApp(const Fluid2DApp&) = delete;
    Fluid2DApp& operator=(const Fluid2DApp&) = delete;

    int run() {
        if (config_.headless) {
            throw std::runtime_error("fluid_2d does not support --headless yet");
        }

        cubey::app::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
                .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                .require_dynamic_rendering = true,
            },
            {
                .create_swapchain_resources =
                    [this](cubey::app::WindowedAppContext& context) {
                        create_global_resources_if_needed(context.device());
                        create_render_pipeline(context.device(), context.swapchain().format(),
                                               context.swapchain().extent());
                    },
                .destroy_swapchain_resources =
                    [this](cubey::app::WindowedAppContext& context) {
                        (void)context;
                        destroy_swapchain_resources();
                    },
                .on_ready =
                    [](cubey::app::WindowedAppContext& context) {
                        setup_input(context);
                        std::printf("fluid_2d: %s rendering 2D fluid project at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update = {},
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                           std::uint32_t image_index, const FrameTiming& timing) {
                        record_frame(context, command_buffer, image_index, timing);
                    },
                .frame_stats_sample =
                    [](cubey::app::WindowedAppContext& context,
                       const FrameTiming& timing) -> std::optional<FrameStatsSample> {
                    const VkExtent2D extent = context.swapchain().extent();
                    return FrameStatsSample{
                        .delta_seconds = timing.delta_seconds,
                        .width = extent.width,
                        .height = extent.height,
                        .triangles = 1,
                    };
                },
                .shutdown =
                    [this](cubey::app::WindowedAppContext& context) {
                        (void)context;
                        destroy_all_resources();
                    },
            });
        return host.run();
    }

  private:
    static void setup_input(cubey::app::WindowedAppContext& context) {
        cubey::app::GlfwWindow* window = &context.window();
        window->set_key_callback([window](const cubey::app::KeyEvent& event) {
            if (event.action != cubey::app::KeyAction::Press) {
                return;
            }
            if (event.key == cubey::app::Key::Escape) {
                window->request_close();
            }
        });
    }

    void destroy_swapchain_resources() {
        render_pipeline_.reset();
        render_pipeline_layout_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        advect_pipeline_.reset();
        inject_pipeline_.reset();
        compute_pipeline_layout_.reset();
        render_descriptors_.reset();
        compute_descriptor_pool_.reset();
        compute_descriptor_layout_.reset();
        field_b_.reset();
        field_a_.reset();
    }

    void create_global_resources_if_needed(cubey::vulkan::Device& device) {
        if (field_a_.has_value()) {
            return;
        }

        create_field_buffers(device);
        create_descriptor_resources(device);
        create_compute_pipelines(device);
    }

    void create_field_buffers(cubey::vulkan::Device& device) {
        const std::vector<FluidCellGpu> initial(field_cell_count(fluid_config_));
        const VkDeviceSize byte_size = static_cast<VkDeviceSize>(field_byte_size(fluid_config_));
        field_a_.emplace(cubey::vulkan::upload_device_buffer(device, initial.data(), byte_size,
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        field_b_.emplace(cubey::vulkan::upload_device_buffer(device, initial.data(), byte_size,
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    }

    void create_descriptor_resources(cubey::vulkan::Device& device) {
        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> compute_bindings{{
            {
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = 1,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        }};
        const cubey::vulkan::DescriptorSetInfo compute_info(compute_bindings, 2);
        compute_descriptor_layout_.emplace(device, compute_info.layout_info());
        compute_descriptor_pool_.emplace(device, compute_info.pool_info());
        inject_descriptor_set_ = compute_descriptor_pool().allocate(compute_descriptor_layout());
        advect_descriptor_set_ = compute_descriptor_pool().allocate(compute_descriptor_layout());

        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> render_bindings{{
            {
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        }};
        const cubey::vulkan::DescriptorSetInfo render_info(render_bindings);
        render_descriptors_.emplace(device, render_info);

        update_field_descriptors(device);
    }

    void update_field_descriptors(cubey::vulkan::Device& device) {
        const cubey::vulkan::DescriptorBufferWrite inject_source =
            cubey::vulkan::storage_buffer_descriptor(inject_descriptor_set_, 0, field_a().handle(),
                                                     field_a().size());
        const cubey::vulkan::DescriptorBufferWrite inject_destination =
            cubey::vulkan::storage_buffer_descriptor(inject_descriptor_set_, 1, field_b().handle(),
                                                     field_b().size());
        const cubey::vulkan::DescriptorBufferWrite advect_source =
            cubey::vulkan::storage_buffer_descriptor(advect_descriptor_set_, 0, field_b().handle(),
                                                     field_b().size());
        const cubey::vulkan::DescriptorBufferWrite advect_destination =
            cubey::vulkan::storage_buffer_descriptor(advect_descriptor_set_, 1, field_a().handle(),
                                                     field_a().size());
        const cubey::vulkan::DescriptorBufferWrite render_source =
            cubey::vulkan::storage_buffer_descriptor(render_descriptors().set(), 0,
                                                     field_a().handle(), field_a().size());
        const std::array<VkWriteDescriptorSet, 5> writes{
            inject_source.descriptor_write(), inject_destination.descriptor_write(),
            advect_source.descriptor_write(), advect_destination.descriptor_write(),
            render_source.descriptor_write(),
        };
        cubey::vulkan::update_descriptor_sets(device, writes);
    }

    void create_compute_pipelines(cubey::vulkan::Device& device) {
        const VkPushConstantRange compute_push_constant{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(SimulationPushConstants),
        };
        const std::array<VkDescriptorSetLayout, 1> set_layouts{compute_descriptor_layout()};
        const std::array<VkPushConstantRange, 1> push_constants{compute_push_constant};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = push_constants,
        });
        compute_pipeline_layout_.emplace(device, layout_info.create_info());

        const std::vector<std::uint32_t> inject_code =
            cubey::read_spirv_file(shader_path("fluid_2d_inject.comp.spv"));
        cubey::vulkan::ShaderModule inject_shader(device, inject_code);
        const VkPipelineShaderStageCreateInfo inject_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, inject_shader.handle());
        const cubey::vulkan::ComputePipelineInfo inject_info({
            .layout = compute_pipeline_layout().handle(),
            .shader_stage = inject_stage,
        });
        inject_pipeline_.emplace(device, inject_info.create_info());

        const std::vector<std::uint32_t> advect_code =
            cubey::read_spirv_file(shader_path("fluid_2d_advect.comp.spv"));
        cubey::vulkan::ShaderModule advect_shader(device, advect_code);
        const VkPipelineShaderStageCreateInfo advect_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, advect_shader.handle());
        const cubey::vulkan::ComputePipelineInfo advect_info({
            .layout = compute_pipeline_layout().handle(),
            .shader_stage = advect_stage,
        });
        advect_pipeline_.emplace(device, advect_info.create_info());
    }

    void create_render_pipeline(cubey::vulkan::Device& device, VkFormat color_format,
                                VkExtent2D extent) {
        const std::vector<std::uint32_t> vertex_code =
            cubey::read_spirv_file(shader_path("fluid_2d.vert.spv"));
        const std::vector<std::uint32_t> fragment_code =
            cubey::read_spirv_file(shader_path("fluid_2d_render.frag.spv"));
        cubey::vulkan::ShaderModule vertex_shader(device, vertex_code);
        cubey::vulkan::ShaderModule fragment_shader(device, fragment_code);

        const VkPipelineShaderStageCreateInfo vertex_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader.handle());
        const VkPipelineShaderStageCreateInfo fragment_stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader.handle());
        const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
            vertex_stage,
            fragment_stage,
        };

        const VkPushConstantRange render_push_constant{
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(RenderPushConstants),
        };
        const std::array<VkDescriptorSetLayout, 1> set_layouts{render_descriptors().layout()};
        const std::array<VkPushConstantRange, 1> push_constants{render_push_constant};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = push_constants,
        });
        render_pipeline_layout_.emplace(device, layout_info.create_info());

        cubey::vulkan::DynamicGraphicsPipelineConfig pipeline_config;
        pipeline_config.layout = render_pipeline_layout().handle();
        pipeline_config.extent = extent;
        pipeline_config.color_format = color_format;
        pipeline_config.shader_stages = shader_stages;
        const cubey::vulkan::DynamicGraphicsPipelineInfo pipeline_info(pipeline_config);
        render_pipeline_.emplace(device, pipeline_info.create_info());
    }

    [[nodiscard]] SimulationPushConstants
    simulation_push_constants(const FrameTiming& timing) const {
        const float time = static_cast<float>(timing.elapsed_seconds);
        const float dt =
            std::min(static_cast<float>(timing.delta_seconds), fluid_config_.fixed_delta_seconds);
        return {
            .grid_dt_time =
                {
                    static_cast<float>(fluid_config_.grid_width),
                    static_cast<float>(fluid_config_.grid_height),
                    dt,
                    time,
                },
            .injection_xy_radius_strength =
                {
                    0.5F + (std::cos(time * 0.73F) * 0.23F),
                    0.5F + (std::sin(time * 0.91F) * 0.18F),
                    kFallbackInjectionRadius,
                    8.0F,
                },
            .injection_dye_active =
                {
                    0.12F + (0.18F * std::sin(time * 0.47F)),
                    0.46F,
                    0.92F,
                    0.0F,
                },
            .force_decay =
                {
                    -std::sin(time * 0.91F) * 1.8F,
                    std::cos(time * 0.73F) * 1.8F,
                    fluid_config_.dye_decay_per_second,
                    fluid_config_.velocity_decay_per_second,
                },
        };
    }

    void record_fluid_compute(VkCommandBuffer command_buffer, const FrameTiming& timing) const {
        const SimulationPushConstants push_constants = simulation_push_constants(timing);
        const std::uint32_t groups_x =
            (fluid_config_.grid_width + fluid_config_.compute_group_size - 1U) /
            fluid_config_.compute_group_size;
        const std::uint32_t groups_y =
            (fluid_config_.grid_height + fluid_config_.compute_group_size - 1U) /
            fluid_config_.compute_group_size;

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          inject_pipeline().handle());
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                compute_pipeline_layout().handle(), 0, 1, &inject_descriptor_set_,
                                0, nullptr);
        vkCmdPushConstants(command_buffer, compute_pipeline_layout().handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
        vkCmdDispatch(command_buffer, groups_x, groups_y, 1);

        auto compute_to_compute = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        compute_to_compute.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        compute_to_compute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &compute_to_compute, 0,
                             nullptr, 0, nullptr);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          advect_pipeline().handle());
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                compute_pipeline_layout().handle(), 0, 1, &advect_descriptor_set_,
                                0, nullptr);
        vkCmdPushConstants(command_buffer, compute_pipeline_layout().handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
        vkCmdDispatch(command_buffer, groups_x, groups_y, 1);

        auto compute_to_fragment = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        compute_to_fragment.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        compute_to_fragment.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &compute_to_fragment, 0,
                             nullptr, 0, nullptr);
    }

    void record_fullscreen_draw(VkCommandBuffer command_buffer, VkImageView image_view,
                                VkExtent2D extent, const FrameTiming& timing) const {
        VkClearValue clear{};
        clear.color = {{0.006F, 0.008F, 0.014F, 1.0F}};
        const VkRenderingAttachmentInfo color_attachment =
            cubey::vulkan::color_rendering_attachment(image_view, clear);

        auto rendering = vk_struct<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
        rendering.renderArea.offset = {0, 0};
        rendering.renderArea.extent = extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_attachment;

        const RenderPushConstants push_constants{
            .grid_time =
                {
                    static_cast<float>(fluid_config_.grid_width),
                    static_cast<float>(fluid_config_.grid_height),
                    static_cast<float>(timing.elapsed_seconds),
                },
        };

        vkCmdBeginRendering(command_buffer, &rendering);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          render_pipeline().handle());
        const VkDescriptorSet descriptor_set = render_descriptors().set();
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                render_pipeline_layout().handle(), 0, 1, &descriptor_set, 0,
                                nullptr);
        vkCmdPushConstants(command_buffer, render_pipeline_layout().handle(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
                           &push_constants);
        vkCmdDraw(command_buffer, 3, 1, 0, 0);
        vkCmdEndRendering(command_buffer);
    }

    void record_frame(cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                      std::uint32_t image_index, const FrameTiming& timing) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        record_fluid_compute(command_buffer, timing);

        cubey::vulkan::Swapchain& swapchain = context.swapchain();
        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain.images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));

        record_fullscreen_draw(command_buffer, swapchain.image_views().at(swapchain_image_index),
                               swapchain.extent(), timing);

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_present_transition(swapchain_image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer fluid_2d");
    }

    [[nodiscard]] const cubey::vulkan::Buffer& field_a() const {
        if (!field_a_.has_value()) {
            throw std::runtime_error("fluid field A is not initialized");
        }
        return field_a_.value();
    }

    [[nodiscard]] const cubey::vulkan::Buffer& field_b() const {
        if (!field_b_.has_value()) {
            throw std::runtime_error("fluid field B is not initialized");
        }
        return field_b_.value();
    }

    [[nodiscard]] VkDescriptorSetLayout compute_descriptor_layout() const {
        if (!compute_descriptor_layout_.has_value()) {
            throw std::runtime_error("compute descriptor layout is not initialized");
        }
        return compute_descriptor_layout_->handle();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorPool& compute_descriptor_pool() const {
        if (!compute_descriptor_pool_.has_value()) {
            throw std::runtime_error("compute descriptor pool is not initialized");
        }
        return compute_descriptor_pool_.value();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorSetBundle& render_descriptors() const {
        if (!render_descriptors_.has_value()) {
            throw std::runtime_error("fluid render descriptors are not initialized");
        }
        return render_descriptors_.value();
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& compute_pipeline_layout() const {
        if (!compute_pipeline_layout_.has_value()) {
            throw std::runtime_error("compute pipeline layout is not initialized");
        }
        return compute_pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::ComputePipeline& inject_pipeline() const {
        if (!inject_pipeline_.has_value()) {
            throw std::runtime_error("inject pipeline is not initialized");
        }
        return inject_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::ComputePipeline& advect_pipeline() const {
        if (!advect_pipeline_.has_value()) {
            throw std::runtime_error("advect pipeline is not initialized");
        }
        return advect_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& render_pipeline_layout() const {
        if (!render_pipeline_layout_.has_value()) {
            throw std::runtime_error("render pipeline layout is not initialized");
        }
        return render_pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::GraphicsPipeline& render_pipeline() const {
        if (!render_pipeline_.has_value()) {
            throw std::runtime_error("render pipeline is not initialized");
        }
        return render_pipeline_.value();
    }

    RunConfig config_;
    Fluid2DConfig fluid_config_;
    std::optional<cubey::vulkan::Buffer> field_a_;
    std::optional<cubey::vulkan::Buffer> field_b_;
    std::optional<cubey::vulkan::DescriptorSetLayout> compute_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> compute_descriptor_pool_;
    VkDescriptorSet inject_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet advect_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_descriptors_;
    std::optional<cubey::vulkan::PipelineLayout> compute_pipeline_layout_;
    std::optional<cubey::vulkan::ComputePipeline> inject_pipeline_;
    std::optional<cubey::vulkan::ComputePipeline> advect_pipeline_;
    std::optional<cubey::vulkan::PipelineLayout> render_pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> render_pipeline_;
};

} // namespace

int run_fluid_2d(const RunConfig& config) {
    Fluid2DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid_2d
