#include "fluid_2d_app.h"

#include <cubey/app/glfw_window.h>
#include <cubey/app/windowed_host.h>
#include <cubey/frame_stats.h>
#include <cubey/headless_png_host.h>
#include <cubey/project_runtime.h>
#include <cubey/spirv_io.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/dynamic_rendering.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/pipeline.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/vk_check.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
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
using cubey::ProjectFrame;
using cubey::vulkan::check;
using cubey::vulkan::vk_struct;

constexpr float kFallbackInjectionRadius = 0.08F;
constexpr float kPointerInjectionRadius = 0.065F;

struct RenderPushConstants {
    std::array<float, 4> grid_debug{};
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

struct DispatchGroups {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

[[nodiscard]] DispatchGroups compute_dispatch_groups(const Fluid2DConfig& config) {
    return {
        .x = (config.grid_width + config.compute_group_size - 1U) / config.compute_group_size,
        .y = (config.grid_height + config.compute_group_size - 1U) / config.compute_group_size,
    };
}

[[nodiscard]] VkPushConstantRange simulation_push_constant_range() {
    return {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(SimulationPushConstants),
    };
}

struct ShaderWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

struct TransferWriteBarrier {
    VkPipelineStageFlags dst_stage = 0;
    VkAccessFlags dst_access = 0;
};

void record_shader_write_barrier(VkCommandBuffer command_buffer, ShaderWriteBarrier config) {
    auto barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, config.dst_stage, 0,
                         1, &barrier, 0, nullptr, 0, nullptr);
}

void record_transfer_write_barrier(VkCommandBuffer command_buffer, TransferWriteBarrier config) {
    auto barrier = vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = config.dst_access;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, config.dst_stage, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
}

[[nodiscard]] float debug_view_push_value(FluidDebugView view) {
    return static_cast<float>(static_cast<std::uint32_t>(view));
}

struct PointerState {
    bool left_down = false;
    bool has_cursor = false;
    cubey::app::CursorPosition cursor{};
    cubey::app::CursorPosition accumulated_delta{};
};

struct FrameInjection {
    bool active = false;
    std::array<float, 2> xy{};
    std::array<float, 2> force{};
};

class Fluid2DApp {
  public:
    explicit Fluid2DApp(RunConfig config) : config_(std::move(config)), runtime_(1) {}

    Fluid2DApp(const Fluid2DApp&) = delete;
    Fluid2DApp& operator=(const Fluid2DApp&) = delete;

    int run() {
        if (config_.headless) {
            return run_headless();
        }

        cubey::app::WindowedHost host(
            {
                .run_config = config_,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
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
                    [this](cubey::app::WindowedAppContext& context) {
                        setup_input(context);
                        std::printf("fluid_2d: %s rendering 2D fluid project at %ux%u\n",
                                    context.device().device_name(),
                                    context.swapchain().extent().width,
                                    context.swapchain().extent().height);
                    },
                .update =
                    [this](cubey::app::WindowedAppContext& context, const FrameTiming& timing) {
                        const ProjectFrame& project_frame = runtime_.frame_for_timing(timing);
                        update_interaction(context, project_frame);
                    },
                .record_frame =
                    [this](cubey::app::WindowedAppContext& context, VkCommandBuffer command_buffer,
                           std::uint32_t image_index, const FrameTiming& timing) {
                        const ProjectFrame& project_frame = runtime_.frame_for_timing(timing);
                        record_frame(context, command_buffer, image_index, project_frame);
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
                        static_cast<void>(runtime_.retire_deferred_destruction());
                    },
            });
        return host.run();
    }

  private:
    void setup_input(cubey::app::WindowedAppContext& context) {
        cubey::app::GlfwWindow* window = &context.window();
        window->set_key_callback([this, window](const cubey::app::KeyEvent& event) {
            if (event.action != cubey::app::KeyAction::Press) {
                return;
            }
            if (event.key == cubey::app::Key::Escape) {
                window->request_close();
            } else if (event.key == cubey::app::Key::Space) {
                paused_ = !paused_;
            } else if (event.key == cubey::app::Key::R) {
                reset_requested_ = true;
            } else if (event.key == cubey::app::Key::D) {
                debug_view_ = next_debug_view(debug_view_);
            }
        });
        window->set_mouse_button_callback([this](const cubey::app::MouseButtonEvent& event) {
            if (event.button != cubey::app::MouseButton::Left) {
                return;
            }
            pointer_.left_down = event.action == cubey::app::MouseButtonAction::Press;
            pointer_.has_cursor = true;
            pointer_.cursor = event.cursor;
            pointer_.accumulated_delta = {};
        });
        window->set_cursor_position_callback([this](const cubey::app::CursorPositionEvent& event) {
            if (pointer_.left_down && pointer_.has_cursor) {
                pointer_.accumulated_delta.x += event.cursor.x - pointer_.cursor.x;
                pointer_.accumulated_delta.y += event.cursor.y - pointer_.cursor.y;
            }
            pointer_.has_cursor = true;
            pointer_.cursor = event.cursor;
        });
    }

    void update_interaction(cubey::app::WindowedAppContext& context,
                            const ProjectFrame& project_frame) {
        (void)project_frame;
        const VkExtent2D extent = context.swapchain().extent();
        frame_injection_ = {};
        if (!pointer_.left_down || !pointer_.has_cursor || extent.width == 0 ||
            extent.height == 0) {
            pointer_.accumulated_delta = {};
            return;
        }

        const float width = static_cast<float>(extent.width);
        const float height = static_cast<float>(extent.height);
        const float cursor_x = static_cast<float>(pointer_.cursor.x);
        const float cursor_y = static_cast<float>(pointer_.cursor.y);
        const float delta_x = static_cast<float>(pointer_.accumulated_delta.x);
        const float delta_y = static_cast<float>(pointer_.accumulated_delta.y);

        frame_injection_ = {
            .active = true,
            .xy =
                {
                    std::clamp(cursor_x / width, 0.0F, 1.0F),
                    std::clamp(1.0F - (cursor_y / height), 0.0F, 1.0F),
                },
            .force =
                {
                    std::clamp((delta_x / width) * 90.0F, -8.0F, 8.0F),
                    std::clamp((-delta_y / height) * 90.0F, -8.0F, 8.0F),
                },
        };
        pointer_.accumulated_delta = {};
    }

    void destroy_swapchain_resources() {
        render_pipeline_.reset();
        render_pipeline_layout_.reset();
    }

    void destroy_all_resources() {
        destroy_swapchain_resources();
        projection_pipeline_.reset();
        pressure_pipeline_.reset();
        divergence_pipeline_.reset();
        advect_pipeline_.reset();
        inject_pipeline_.reset();
        projection_pipeline_layout_.reset();
        pressure_pipeline_layout_.reset();
        divergence_pipeline_layout_.reset();
        compute_pipeline_layout_.reset();
        render_descriptors_.reset();
        projection_descriptor_pool_.reset();
        projection_descriptor_layout_.reset();
        pressure_descriptor_pool_.reset();
        pressure_descriptor_layout_.reset();
        divergence_descriptor_pool_.reset();
        divergence_descriptor_layout_.reset();
        compute_descriptor_pool_.reset();
        compute_descriptor_layout_.reset();
        pressure_b_.reset();
        pressure_a_.reset();
        divergence_.reset();
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

        const std::vector<float> scalar_initial(field_cell_count(fluid_config_), 0.0F);
        const VkDeviceSize scalar_byte_size =
            static_cast<VkDeviceSize>(scalar_field_byte_size(fluid_config_));
        divergence_.emplace(cubey::vulkan::upload_device_buffer(
            device, scalar_initial.data(), scalar_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        pressure_a_.emplace(cubey::vulkan::upload_device_buffer(
            device, scalar_initial.data(), scalar_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        pressure_b_.emplace(cubey::vulkan::upload_device_buffer(
            device, scalar_initial.data(), scalar_byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
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

        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> divergence_bindings{{
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
            {
                .binding = 2,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
            {
                .binding = 3,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        }};
        const cubey::vulkan::DescriptorSetInfo divergence_info(divergence_bindings);
        divergence_descriptor_layout_.emplace(device, divergence_info.layout_info());
        divergence_descriptor_pool_.emplace(device, divergence_info.pool_info());
        divergence_descriptor_set_ =
            divergence_descriptor_pool().allocate(divergence_descriptor_layout());

        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> pressure_bindings{{
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
            {
                .binding = 2,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
            },
        }};
        const cubey::vulkan::DescriptorSetInfo pressure_info(pressure_bindings, 2);
        pressure_descriptor_layout_.emplace(device, pressure_info.layout_info());
        pressure_descriptor_pool_.emplace(device, pressure_info.pool_info());
        pressure_a_to_b_descriptor_set_ =
            pressure_descriptor_pool().allocate(pressure_descriptor_layout());
        pressure_b_to_a_descriptor_set_ =
            pressure_descriptor_pool().allocate(pressure_descriptor_layout());

        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> projection_bindings{{
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
        const cubey::vulkan::DescriptorSetInfo projection_info(projection_bindings, 2);
        projection_descriptor_layout_.emplace(device, projection_info.layout_info());
        projection_descriptor_pool_.emplace(device, projection_info.pool_info());
        projection_pressure_a_descriptor_set_ =
            projection_descriptor_pool().allocate(projection_descriptor_layout());
        projection_pressure_b_descriptor_set_ =
            projection_descriptor_pool().allocate(projection_descriptor_layout());

        const std::array<cubey::vulkan::DescriptorSetBindingConfig, 4> render_bindings{{
            {
                .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 1,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 2,
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding = 3,
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
        const cubey::vulkan::DescriptorBufferWrite render_divergence =
            cubey::vulkan::storage_buffer_descriptor(render_descriptors().set(), 1,
                                                     divergence().handle(), divergence().size());
        const cubey::vulkan::DescriptorBufferWrite render_pressure_a =
            cubey::vulkan::storage_buffer_descriptor(render_descriptors().set(), 2,
                                                     pressure_a().handle(), pressure_a().size());
        const cubey::vulkan::DescriptorBufferWrite render_pressure_b =
            cubey::vulkan::storage_buffer_descriptor(render_descriptors().set(), 3,
                                                     pressure_b().handle(), pressure_b().size());
        const cubey::vulkan::DescriptorBufferWrite divergence_source =
            cubey::vulkan::storage_buffer_descriptor(divergence_descriptor_set_, 0,
                                                     field_a().handle(), field_a().size());
        const cubey::vulkan::DescriptorBufferWrite divergence_destination =
            cubey::vulkan::storage_buffer_descriptor(divergence_descriptor_set_, 1,
                                                     divergence().handle(), divergence().size());
        const cubey::vulkan::DescriptorBufferWrite divergence_pressure_a =
            cubey::vulkan::storage_buffer_descriptor(divergence_descriptor_set_, 2,
                                                     pressure_a().handle(), pressure_a().size());
        const cubey::vulkan::DescriptorBufferWrite divergence_pressure_b =
            cubey::vulkan::storage_buffer_descriptor(divergence_descriptor_set_, 3,
                                                     pressure_b().handle(), pressure_b().size());
        const cubey::vulkan::DescriptorBufferWrite pressure_a_to_b_divergence =
            cubey::vulkan::storage_buffer_descriptor(pressure_a_to_b_descriptor_set_, 0,
                                                     divergence().handle(), divergence().size());
        const cubey::vulkan::DescriptorBufferWrite pressure_a_to_b_source =
            cubey::vulkan::storage_buffer_descriptor(pressure_a_to_b_descriptor_set_, 1,
                                                     pressure_a().handle(), pressure_a().size());
        const cubey::vulkan::DescriptorBufferWrite pressure_a_to_b_destination =
            cubey::vulkan::storage_buffer_descriptor(pressure_a_to_b_descriptor_set_, 2,
                                                     pressure_b().handle(), pressure_b().size());
        const cubey::vulkan::DescriptorBufferWrite pressure_b_to_a_divergence =
            cubey::vulkan::storage_buffer_descriptor(pressure_b_to_a_descriptor_set_, 0,
                                                     divergence().handle(), divergence().size());
        const cubey::vulkan::DescriptorBufferWrite pressure_b_to_a_source =
            cubey::vulkan::storage_buffer_descriptor(pressure_b_to_a_descriptor_set_, 1,
                                                     pressure_b().handle(), pressure_b().size());
        const cubey::vulkan::DescriptorBufferWrite pressure_b_to_a_destination =
            cubey::vulkan::storage_buffer_descriptor(pressure_b_to_a_descriptor_set_, 2,
                                                     pressure_a().handle(), pressure_a().size());
        const cubey::vulkan::DescriptorBufferWrite projection_pressure_a_field =
            cubey::vulkan::storage_buffer_descriptor(projection_pressure_a_descriptor_set_, 0,
                                                     field_a().handle(), field_a().size());
        const cubey::vulkan::DescriptorBufferWrite projection_pressure_a_pressure =
            cubey::vulkan::storage_buffer_descriptor(projection_pressure_a_descriptor_set_, 1,
                                                     pressure_a().handle(), pressure_a().size());
        const cubey::vulkan::DescriptorBufferWrite projection_pressure_b_field =
            cubey::vulkan::storage_buffer_descriptor(projection_pressure_b_descriptor_set_, 0,
                                                     field_a().handle(), field_a().size());
        const cubey::vulkan::DescriptorBufferWrite projection_pressure_b_pressure =
            cubey::vulkan::storage_buffer_descriptor(projection_pressure_b_descriptor_set_, 1,
                                                     pressure_b().handle(), pressure_b().size());

        const std::array<cubey::vulkan::DescriptorBufferWrite, 22> buffer_writes{
            inject_source,
            inject_destination,
            advect_source,
            advect_destination,
            render_source,
            render_divergence,
            render_pressure_a,
            render_pressure_b,
            divergence_source,
            divergence_destination,
            divergence_pressure_a,
            divergence_pressure_b,
            pressure_a_to_b_divergence,
            pressure_a_to_b_source,
            pressure_a_to_b_destination,
            pressure_b_to_a_divergence,
            pressure_b_to_a_source,
            pressure_b_to_a_destination,
            projection_pressure_a_field,
            projection_pressure_a_pressure,
            projection_pressure_b_field,
            projection_pressure_b_pressure,
        };
        std::array<VkWriteDescriptorSet, buffer_writes.size()> writes{};
        for (std::size_t index = 0; index < buffer_writes.size(); ++index) {
            writes[index] = buffer_writes[index].descriptor_write();
        }
        cubey::vulkan::update_descriptor_sets(device, writes);
    }

    static void create_compute_layout(cubey::vulkan::Device& device,
                                      VkDescriptorSetLayout descriptor_layout,
                                      std::optional<cubey::vulkan::PipelineLayout>& destination) {
        const VkPushConstantRange compute_push_constant = simulation_push_constant_range();
        const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptor_layout};
        const std::array<VkPushConstantRange, 1> push_constants{compute_push_constant};
        const cubey::vulkan::PipelineLayoutInfo layout_info({
            .set_layouts = set_layouts,
            .push_constants = push_constants,
        });
        destination.emplace(device, layout_info.create_info());
    }

    static void create_compute_pipeline_from_shader(
        cubey::vulkan::Device& device, const char* filename,
        const cubey::vulkan::PipelineLayout& pipeline_layout,
        std::optional<cubey::vulkan::ComputePipeline>& destination) {
        const std::vector<std::uint32_t> code = cubey::read_spirv_file(shader_path(filename));
        cubey::vulkan::ShaderModule shader(device, code);
        const VkPipelineShaderStageCreateInfo stage =
            cubey::vulkan::shader_stage(VK_SHADER_STAGE_COMPUTE_BIT, shader.handle());
        const cubey::vulkan::ComputePipelineInfo info({
            .layout = pipeline_layout.handle(),
            .shader_stage = stage,
        });
        destination.emplace(device, info.create_info());
    }

    void create_compute_pipelines(cubey::vulkan::Device& device) {
        create_compute_layout(device, compute_descriptor_layout(), compute_pipeline_layout_);
        create_compute_pipeline_from_shader(device, "fluid_2d_inject.comp.spv",
                                            compute_pipeline_layout(), inject_pipeline_);
        create_compute_pipeline_from_shader(device, "fluid_2d_advect.comp.spv",
                                            compute_pipeline_layout(), advect_pipeline_);

        create_compute_layout(device, divergence_descriptor_layout(), divergence_pipeline_layout_);
        create_compute_pipeline_from_shader(device, "fluid_2d_divergence.comp.spv",
                                            divergence_pipeline_layout(), divergence_pipeline_);

        create_compute_layout(device, pressure_descriptor_layout(), pressure_pipeline_layout_);
        create_compute_pipeline_from_shader(device, "fluid_2d_pressure.comp.spv",
                                            pressure_pipeline_layout(), pressure_pipeline_);

        create_compute_layout(device, projection_descriptor_layout(), projection_pipeline_layout_);
        create_compute_pipeline_from_shader(device, "fluid_2d_projection.comp.spv",
                                            projection_pipeline_layout(), projection_pipeline_);
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
    simulation_push_constants(const ProjectFrame& frame) const {
        const float time = static_cast<float>(frame.elapsed_seconds);
        const float dt =
            std::min(static_cast<float>(frame.delta_seconds), fluid_config_.fixed_delta_seconds);
        const bool pointer_active = frame_injection_.active;
        const float injection_x =
            pointer_active ? frame_injection_.xy[0] : 0.5F + (std::cos(time * 0.73F) * 0.23F);
        const float injection_y =
            pointer_active ? frame_injection_.xy[1] : 0.5F + (std::sin(time * 0.91F) * 0.18F);
        const float injection_radius =
            pointer_active ? kPointerInjectionRadius : kFallbackInjectionRadius;
        const float injection_strength = pointer_active ? 11.0F : 8.0F;
        const std::array<float, 3> injection_dye =
            pointer_active
                ? std::array<float, 3>{0.98F, 0.32F, 0.13F}
                : std::array<float, 3>{0.12F + (0.18F * std::sin(time * 0.47F)), 0.46F, 0.92F};
        const float force_x =
            pointer_active ? frame_injection_.force[0] : -std::sin(time * 0.91F) * 1.8F;
        const float force_y =
            pointer_active ? frame_injection_.force[1] : std::cos(time * 0.73F) * 1.8F;
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
                    injection_x,
                    injection_y,
                    injection_radius,
                    injection_strength,
                },
            .injection_dye_active =
                {
                    injection_dye[0],
                    injection_dye[1],
                    injection_dye[2],
                    1.0F,
                },
            .force_decay =
                {
                    force_x,
                    force_y,
                    fluid_config_.dye_decay_per_second,
                    fluid_config_.velocity_decay_per_second,
                },
        };
    }

    void record_field_reset(VkCommandBuffer command_buffer) const {
        vkCmdFillBuffer(command_buffer, field_a().handle(), 0, field_a().size(), 0);
        vkCmdFillBuffer(command_buffer, field_b().handle(), 0, field_b().size(), 0);
        vkCmdFillBuffer(command_buffer, divergence().handle(), 0, divergence().size(), 0);
        vkCmdFillBuffer(command_buffer, pressure_a().handle(), 0, pressure_a().size(), 0);
        vkCmdFillBuffer(command_buffer, pressure_b().handle(), 0, pressure_b().size(), 0);
        record_transfer_write_barrier(
            command_buffer,
            {
                .dst_stage =
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });
    }

    void record_fluid_compute(VkCommandBuffer command_buffer, const ProjectFrame& frame) {
        if (reset_requested_) {
            record_field_reset(command_buffer);
            reset_requested_ = false;
        }
        if (paused_) {
            return;
        }

        const SimulationPushConstants push_constants = simulation_push_constants(frame);
        const DispatchGroups groups = compute_dispatch_groups(fluid_config_);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          inject_pipeline().handle());
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                compute_pipeline_layout().handle(), 0, 1, &inject_descriptor_set_,
                                0, nullptr);
        vkCmdPushConstants(command_buffer, compute_pipeline_layout().handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
        vkCmdDispatch(command_buffer, groups.x, groups.y, 1);

        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          advect_pipeline().handle());
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                compute_pipeline_layout().handle(), 0, 1, &advect_descriptor_set_,
                                0, nullptr);
        vkCmdPushConstants(command_buffer, compute_pipeline_layout().handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
        vkCmdDispatch(command_buffer, groups.x, groups.y, 1);

        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          divergence_pipeline().handle());
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                divergence_pipeline_layout().handle(), 0, 1,
                                &divergence_descriptor_set_, 0, nullptr);
        vkCmdPushConstants(command_buffer, divergence_pipeline_layout().handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
        vkCmdDispatch(command_buffer, groups.x, groups.y, 1);

        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          pressure_pipeline().handle());
        for (std::uint32_t iteration = 0; iteration < fluid_config_.pressure_iterations;
             ++iteration) {
            const VkDescriptorSet descriptor_set = (iteration % 2U == 0)
                                                       ? pressure_a_to_b_descriptor_set_
                                                       : pressure_b_to_a_descriptor_set_;
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    pressure_pipeline_layout().handle(), 0, 1, &descriptor_set, 0,
                                    nullptr);
            vkCmdPushConstants(command_buffer, pressure_pipeline_layout().handle(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                               &push_constants);
            vkCmdDispatch(command_buffer, groups.x, groups.y, 1);
            record_shader_write_barrier(
                command_buffer,
                {
                    .dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                });
        }

        const bool final_pressure_is_a = (fluid_config_.pressure_iterations % 2U) == 0;
        const VkDescriptorSet projection_descriptor_set =
            final_pressure_is_a ? projection_pressure_a_descriptor_set_
                                : projection_pressure_b_descriptor_set_;
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          projection_pipeline().handle());
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                projection_pipeline_layout().handle(), 0, 1,
                                &projection_descriptor_set, 0, nullptr);
        vkCmdPushConstants(command_buffer, projection_pipeline_layout().handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
        vkCmdDispatch(command_buffer, groups.x, groups.y, 1);

        record_shader_write_barrier(
            command_buffer,
            {
                .dst_stage =
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                .dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            });
    }

    void record_fullscreen_draw(VkCommandBuffer command_buffer, VkImageView image_view,
                                VkExtent2D extent) const {
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
            .grid_debug =
                {
                    static_cast<float>(fluid_config_.grid_width),
                    static_cast<float>(fluid_config_.grid_height),
                    debug_view_push_value(debug_view_),
                    (fluid_config_.pressure_iterations % 2U) == 0 ? 0.0F : 1.0F,
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
                      std::uint32_t image_index, const ProjectFrame& frame) {
        cubey::vulkan::begin_command_buffer(command_buffer,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        record_fluid_compute(command_buffer, frame);

        cubey::vulkan::Swapchain& swapchain = context.swapchain();
        const std::size_t swapchain_image_index = static_cast<std::size_t>(image_index);
        const VkImage swapchain_image = swapchain.images().at(swapchain_image_index);
        cubey::vulkan::transition_image_layout(
            command_buffer, cubey::vulkan::begin_color_attachment_transition(swapchain_image));

        record_fullscreen_draw(command_buffer, swapchain.image_views().at(swapchain_image_index),
                               swapchain.extent());

        cubey::vulkan::transition_image_layout(
            command_buffer,
            cubey::vulkan::finish_color_attachment_for_present_transition(swapchain_image));

        check(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer fluid_2d");
    }

    void record_headless_simulation_frame(cubey::vulkan::Device& device,
                                          const ProjectFrame& frame) {
        cubey::vulkan::ImmediateCommands commands(device);
        record_fluid_compute(commands.command_buffer(), frame);
        commands.submit_and_wait();
    }

    int run_headless() {
        cubey::HeadlessPngHostConfig host_config;
        host_config.run_config = config_;
        host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        cubey::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [this](cubey::HeadlessPngContext& context) {
            const cubey::HeadlessRenderTarget& target = context.render_target();
            create_global_resources_if_needed(context.device());
            create_render_pipeline(context.device(), target.format, target.extent);
        };
        callbacks.before_capture = [this](cubey::HeadlessPngContext& context) {
            const std::uint32_t frames = headless_frame_count(config_);
            for (std::uint32_t frame = 1; frame <= frames; ++frame) {
                const ProjectFrame project_frame =
                    runtime_.frame_for_timing(fixed_headless_timing(fluid_config_, frame));
                record_headless_simulation_frame(context.device(), project_frame);
            }
        };
        callbacks.record_capture = [this](cubey::HeadlessPngContext&,
                                          VkCommandBuffer command_buffer,
                                          const cubey::HeadlessRenderTarget& target) {
            record_fullscreen_draw(command_buffer, target.view, target.extent);
        };
        callbacks.shutdown = [this](cubey::HeadlessPngContext&) {
            destroy_all_resources();
            static_cast<void>(runtime_.retire_deferred_destruction());
        };

        cubey::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
        return host.run();
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

    [[nodiscard]] const cubey::vulkan::Buffer& divergence() const {
        if (!divergence_.has_value()) {
            throw std::runtime_error("fluid divergence field is not initialized");
        }
        return divergence_.value();
    }

    [[nodiscard]] const cubey::vulkan::Buffer& pressure_a() const {
        if (!pressure_a_.has_value()) {
            throw std::runtime_error("fluid pressure field A is not initialized");
        }
        return pressure_a_.value();
    }

    [[nodiscard]] const cubey::vulkan::Buffer& pressure_b() const {
        if (!pressure_b_.has_value()) {
            throw std::runtime_error("fluid pressure field B is not initialized");
        }
        return pressure_b_.value();
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

    [[nodiscard]] VkDescriptorSetLayout divergence_descriptor_layout() const {
        if (!divergence_descriptor_layout_.has_value()) {
            throw std::runtime_error("divergence descriptor layout is not initialized");
        }
        return divergence_descriptor_layout_->handle();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorPool& divergence_descriptor_pool() const {
        if (!divergence_descriptor_pool_.has_value()) {
            throw std::runtime_error("divergence descriptor pool is not initialized");
        }
        return divergence_descriptor_pool_.value();
    }

    [[nodiscard]] VkDescriptorSetLayout pressure_descriptor_layout() const {
        if (!pressure_descriptor_layout_.has_value()) {
            throw std::runtime_error("pressure descriptor layout is not initialized");
        }
        return pressure_descriptor_layout_->handle();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorPool& pressure_descriptor_pool() const {
        if (!pressure_descriptor_pool_.has_value()) {
            throw std::runtime_error("pressure descriptor pool is not initialized");
        }
        return pressure_descriptor_pool_.value();
    }

    [[nodiscard]] VkDescriptorSetLayout projection_descriptor_layout() const {
        if (!projection_descriptor_layout_.has_value()) {
            throw std::runtime_error("projection descriptor layout is not initialized");
        }
        return projection_descriptor_layout_->handle();
    }

    [[nodiscard]] const cubey::vulkan::DescriptorPool& projection_descriptor_pool() const {
        if (!projection_descriptor_pool_.has_value()) {
            throw std::runtime_error("projection descriptor pool is not initialized");
        }
        return projection_descriptor_pool_.value();
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

    [[nodiscard]] const cubey::vulkan::PipelineLayout& divergence_pipeline_layout() const {
        if (!divergence_pipeline_layout_.has_value()) {
            throw std::runtime_error("divergence pipeline layout is not initialized");
        }
        return divergence_pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& pressure_pipeline_layout() const {
        if (!pressure_pipeline_layout_.has_value()) {
            throw std::runtime_error("pressure pipeline layout is not initialized");
        }
        return pressure_pipeline_layout_.value();
    }

    [[nodiscard]] const cubey::vulkan::PipelineLayout& projection_pipeline_layout() const {
        if (!projection_pipeline_layout_.has_value()) {
            throw std::runtime_error("projection pipeline layout is not initialized");
        }
        return projection_pipeline_layout_.value();
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

    [[nodiscard]] const cubey::vulkan::ComputePipeline& divergence_pipeline() const {
        if (!divergence_pipeline_.has_value()) {
            throw std::runtime_error("divergence pipeline is not initialized");
        }
        return divergence_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::ComputePipeline& pressure_pipeline() const {
        if (!pressure_pipeline_.has_value()) {
            throw std::runtime_error("pressure pipeline is not initialized");
        }
        return pressure_pipeline_.value();
    }

    [[nodiscard]] const cubey::vulkan::ComputePipeline& projection_pipeline() const {
        if (!projection_pipeline_.has_value()) {
            throw std::runtime_error("projection pipeline is not initialized");
        }
        return projection_pipeline_.value();
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
    cubey::ProjectRuntimeAdapter runtime_;
    Fluid2DConfig fluid_config_;
    PointerState pointer_;
    FrameInjection frame_injection_;
    FluidDebugView debug_view_ = FluidDebugView::Dye;
    bool paused_ = false;
    bool reset_requested_ = false;
    std::optional<cubey::vulkan::Buffer> field_a_;
    std::optional<cubey::vulkan::Buffer> field_b_;
    std::optional<cubey::vulkan::Buffer> divergence_;
    std::optional<cubey::vulkan::Buffer> pressure_a_;
    std::optional<cubey::vulkan::Buffer> pressure_b_;
    std::optional<cubey::vulkan::DescriptorSetLayout> compute_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> compute_descriptor_pool_;
    VkDescriptorSet inject_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet advect_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> divergence_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> divergence_descriptor_pool_;
    VkDescriptorSet divergence_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> pressure_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> pressure_descriptor_pool_;
    VkDescriptorSet pressure_a_to_b_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet pressure_b_to_a_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetLayout> projection_descriptor_layout_;
    std::optional<cubey::vulkan::DescriptorPool> projection_descriptor_pool_;
    VkDescriptorSet projection_pressure_a_descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSet projection_pressure_b_descriptor_set_ = VK_NULL_HANDLE;
    std::optional<cubey::vulkan::DescriptorSetBundle> render_descriptors_;
    std::optional<cubey::vulkan::PipelineLayout> compute_pipeline_layout_;
    std::optional<cubey::vulkan::PipelineLayout> divergence_pipeline_layout_;
    std::optional<cubey::vulkan::PipelineLayout> pressure_pipeline_layout_;
    std::optional<cubey::vulkan::PipelineLayout> projection_pipeline_layout_;
    std::optional<cubey::vulkan::ComputePipeline> inject_pipeline_;
    std::optional<cubey::vulkan::ComputePipeline> advect_pipeline_;
    std::optional<cubey::vulkan::ComputePipeline> divergence_pipeline_;
    std::optional<cubey::vulkan::ComputePipeline> pressure_pipeline_;
    std::optional<cubey::vulkan::ComputePipeline> projection_pipeline_;
    std::optional<cubey::vulkan::PipelineLayout> render_pipeline_layout_;
    std::optional<cubey::vulkan::GraphicsPipeline> render_pipeline_;
};

} // namespace

int run_fluid_2d(const RunConfig& config) {
    Fluid2DApp app(config);
    return app.run();
}

} // namespace cubey::projects::fluid_2d
