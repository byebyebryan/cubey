#include "render_graph_test_helpers.h"

#include <cubey/render/render_graph.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <optional>
#include <utility>

using namespace cubey::tests::render_graph;

void test_render_graph_frame_executor_reports_slot_lifecycle_metrics() {
    cubey::render::RenderGraphBuilder graph;
    graph.add_pass("noop", cubey::render::RenderGraphQueueDomain::Graphics)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    cubey::render::RenderGraphBuilder changed_graph;
    static_cast<void>(changed_graph.import_buffer(buffer_desc("changed"), buffer(0x9A)));
    changed_graph.add_pass("noop", cubey::render::RenderGraphQueueDomain::Graphics)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph changed_compiled = changed_graph.compile();

    cubey::render::RenderGraphFrameExecutor executor(1);
    const auto* fake_device = reinterpret_cast<const cubey::vulkan::Device*>(0x9B);
    const VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(0x9C);
    cubey::render::RenderGraphFrameRecordMetrics metrics;
    const cubey::render::FrameSlot slot{.index = 0, .count = 1};
    cubey::render::RenderGraphFrameRecordInfo info{
        .device = fake_device,
        .command_buffer = command_buffer,
        .frame_slot = slot,
        .command_buffer_mode = cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
        .metrics = &metrics,
    };

    executor.record(info, compiled);
    require(metrics.slot_action == cubey::render::RenderGraphFrameSlotAction::Created,
            "first graph frame record should create a frame slot resource set");
    require(metrics.resource_prepare_milliseconds >= 0.0 &&
                metrics.graph_record_milliseconds >= 0.0,
            "graph frame metrics should report nonnegative CPU durations");

    metrics = {
        .slot_action = cubey::render::RenderGraphFrameSlotAction::Replaced,
        .resource_prepare_milliseconds = 17.0,
        .graph_record_milliseconds = 18.0,
    };
    require_throws(
        [&] {
            executor.record(info, compiled, [](const cubey::render::RenderGraphResourceSet&) {
                throw std::runtime_error("test prepare failure");
            });
        },
        "graph frame executor should propagate prepare failures");
    require(metrics.slot_action == cubey::render::RenderGraphFrameSlotAction::Unknown &&
                metrics.resource_prepare_milliseconds == 0.0 &&
                metrics.graph_record_milliseconds == 0.0,
            "graph frame executor should reset caller metrics before a valid record attempt");

    executor.record(info, compiled);
    require(metrics.slot_action == cubey::render::RenderGraphFrameSlotAction::Reused,
            "compatible graph frame record should reuse its frame slot resource set");

    executor.record(info, changed_compiled);
    require(metrics.slot_action == cubey::render::RenderGraphFrameSlotAction::Replaced,
            "incompatible graph frame record should replace its frame slot resource set");
}

void test_render_graph_frame_resources_manage_frame_slots() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle transient =
        graph.create_buffer(buffer_desc("slot buffer"));
    graph.add_pass("simulate", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(transient)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    cubey::render::RenderGraphFrameResources frame_resources;
    require(frame_resources.frame_slot_count() == 0,
            "default graph frame resources should start empty");

    frame_resources.resize(2);
    require(frame_resources.frame_slot_count() == 2,
            "graph frame resources should report resized slot count");

    const cubey::render::FrameSlot slot{
        .index = 1,
        .count = 2,
    };
    cubey::render::RenderGraphResourceSet& resources = frame_resources.emplace(slot, compiled);
    resources.bind_buffer(transient, cubey::render::RenderGraphResolvedBuffer{
                                         .buffer = buffer(0x901),
                                         .byte_size = buffer_desc("slot buffer").byte_size,
                                     });

    const std::optional<cubey::render::RenderGraphResolvedBuffer> resolved =
        frame_resources.resource_set(slot).buffer(transient);
    require(resolved.has_value(), "frame resource set should expose inserted slot resources");
    require(resolved->buffer == buffer(0x901),
            "frame resource set should preserve slot-specific resolved buffer");

    frame_resources.clear();
    require(frame_resources.frame_slot_count() == 0,
            "cleared graph frame resources should report no slots");
}

void test_render_graph_frame_resources_reuse_compatible_slots() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle buffer_handle =
        graph.import_buffer(buffer_desc("slot buffer"), buffer(0x921), host_written_buffer_state());
    graph.add_pass("read", cubey::render::RenderGraphQueueDomain::Compute)
        .read_storage_buffer(buffer_handle)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    cubey::render::RenderGraphBuilder renamed_graph;
    const cubey::render::RenderGraphBufferHandle renamed_buffer = renamed_graph.import_buffer(
        buffer_desc("renamed slot buffer"), buffer(0x921), host_written_buffer_state());
    renamed_graph.add_pass("read", cubey::render::RenderGraphQueueDomain::Compute)
        .read_storage_buffer(renamed_buffer)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph renamed_compiled = renamed_graph.compile();

    cubey::render::RenderGraphBuilder rebound_graph;
    const cubey::render::RenderGraphBufferHandle rebound_buffer =
        rebound_graph.import_buffer(buffer_desc("renamed slot buffer"), buffer(0x923), std::nullopt,
                                    cubey::render::RenderGraphBufferState{
                                        .access_mask = VK_ACCESS_TRANSFER_READ_BIT,
                                        .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    });
    rebound_graph.add_pass("read", cubey::render::RenderGraphQueueDomain::Compute)
        .read_storage_buffer(rebound_buffer)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph rebound_compiled = rebound_graph.compile();

    cubey::render::RenderGraphFrameResources frame_resources(1);
    const cubey::render::FrameSlot slot{
        .index = 0,
        .count = 1,
    };
    cubey::render::RenderGraphResourceSet& first = frame_resources.emplace(slot, compiled);
    first.bind_buffer(buffer_handle, cubey::render::RenderGraphResolvedBuffer{
                                         .buffer = buffer(0x922),
                                         .byte_size = buffer_desc("slot buffer").byte_size,
                                     });

    cubey::render::RenderGraphResourceSet& second = frame_resources.emplace(slot, renamed_compiled);
    require(&first == &second, "compatible graph frame resources should reuse the slot object");
    require(!second.buffer(buffer_handle).has_value(),
            "reused graph frame resources should reset imported bindings before prepare");
    require(second.compatible(renamed_compiled),
            "label-only graph resource renames should reuse graph resources");

    cubey::render::RenderGraphResourceSet& third = frame_resources.emplace(slot, rebound_compiled);
    require(&second == &third && third.compatible(rebound_compiled),
            "imported handles and synchronization state should not replace allocation-compatible "
            "slots");
}

void test_render_graph_resource_set_rejects_incompatible_shapes() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle original =
        graph.create_buffer(buffer_desc("slot buffer"));
    graph.add_pass("write", cubey::render::RenderGraphQueueDomain::Compute)
        .write_storage_buffer(original)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    cubey::render::RenderGraphBuilder changed_graph;
    const cubey::render::RenderGraphBufferHandle changed =
        changed_graph.create_buffer(cubey::render::RenderGraphBufferDesc{
            .label = "slot buffer",
            .byte_size = buffer_desc("slot buffer").byte_size + 4,
        });
    changed_graph.add_pass("write", cubey::render::RenderGraphQueueDomain::Compute)
        .write_storage_buffer(changed)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph changed_compiled = changed_graph.compile();

    cubey::render::RenderGraphResourceSet resources(compiled);
    require(resources.compatible(compiled),
            "resource set should report compatible with the graph that created it");
    require(!resources.compatible(changed_compiled),
            "resource set should reject incompatible graph resource descriptors");

    cubey::render::RenderGraphBuilder changed_usage_graph;
    const cubey::render::RenderGraphBufferHandle changed_usage =
        changed_usage_graph.create_buffer(buffer_desc("slot buffer"));
    changed_usage_graph.add_pass("copy", cubey::render::RenderGraphQueueDomain::Transfer)
        .transfer_write_buffer(changed_usage)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph changed_usage_compiled = changed_usage_graph.compile();
    require(!resources.compatible(changed_usage_compiled),
            "resource set should reject changed aggregate buffer usage requirements");

    cubey::render::RenderGraphBuilder changed_lifetime_graph;
    const cubey::render::RenderGraphBufferHandle changed_lifetime =
        changed_lifetime_graph.import_buffer(buffer_desc("slot buffer"), buffer(0x933));
    changed_lifetime_graph.add_pass("write", cubey::render::RenderGraphQueueDomain::Compute)
        .write_storage_buffer(changed_lifetime)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph changed_lifetime_compiled =
        changed_lifetime_graph.compile();
    require(!resources.compatible(changed_lifetime_compiled),
            "resource set should reject changed resource lifetimes");
}

void test_render_graph_resource_set_rejects_incompatible_texture_requirements() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle original =
        graph.create_texture(color_texture_desc("scene color"));
    graph.add_pass("draw", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(original)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    const cubey::render::RenderGraphResourceSet resources(compiled);

    auto compile_color_target = [](cubey::render::RenderGraphTextureDesc desc) {
        cubey::render::RenderGraphBuilder changed_graph;
        const cubey::render::RenderGraphTextureHandle texture =
            changed_graph.create_texture(std::move(desc));
        changed_graph.add_pass("draw", cubey::render::RenderGraphQueueDomain::Graphics)
            .write_color(texture)
            .execute([](const cubey::render::RenderGraphExecutionContext&) {});
        return changed_graph.compile();
    };

    cubey::render::RenderGraphTextureDesc changed_extent = color_texture_desc("scene color");
    ++changed_extent.extent.width;
    require(!resources.compatible(compile_color_target(changed_extent)),
            "resource set should reject changed texture extents");

    cubey::render::RenderGraphTextureDesc changed_format = color_texture_desc("scene color");
    changed_format.format = VK_FORMAT_B8G8R8A8_UNORM;
    require(!resources.compatible(compile_color_target(changed_format)),
            "resource set should reject changed texture formats");

    cubey::render::RenderGraphBuilder changed_aspects_graph;
    cubey::render::RenderGraphTextureDesc changed_aspects = color_texture_desc("scene color");
    changed_aspects.aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
    const cubey::render::RenderGraphTextureHandle changed_aspects_texture =
        changed_aspects_graph.create_texture(changed_aspects);
    changed_aspects_graph.add_pass("draw", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_depth(changed_aspects_texture)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    require(!resources.compatible(changed_aspects_graph.compile()),
            "resource set should reject changed texture aspects");
}

void test_render_graph_resource_set_rejects_undersized_bound_buffers() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle transient =
        graph.create_buffer(buffer_desc("slot buffer"));
    graph.add_pass("write", cubey::render::RenderGraphQueueDomain::Compute)
        .write_storage_buffer(transient)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    cubey::render::RenderGraphResourceSet resources(compiled);

    require_throws(
        [&] {
            resources.bind_buffer(transient, cubey::render::RenderGraphResolvedBuffer{
                                                 .buffer = buffer(0x941),
                                                 .byte_size =
                                                     buffer_desc("slot buffer").byte_size - 1,
                                             });
        },
        "resource set should reject external buffers smaller than the graph declaration");

    resources.bind_buffer(transient, cubey::render::RenderGraphResolvedBuffer{
                                         .buffer = buffer(0x942),
                                         .byte_size = buffer_desc("slot buffer").byte_size + 1,
                                     });
    require(resources.buffer(transient).has_value(),
            "resource set should accept external buffers large enough for the graph declaration");
}

void test_render_graph_frame_resources_reject_invalid_slots() {
    cubey::render::RenderGraphFrameResources frame_resources(2);

    require_throws(
        [&] {
            (void)frame_resources.resource_set(cubey::render::FrameSlot{
                .index = 0,
                .count = 0,
            });
        },
        "graph frame resources should reject zero-count frame slots");
    require_throws(
        [&] {
            (void)frame_resources.resource_set(cubey::render::FrameSlot{
                .index = 2,
                .count = 2,
            });
        },
        "graph frame resources should reject out-of-range frame slots");
    require_throws(
        [&] {
            (void)frame_resources.resource_set(cubey::render::FrameSlot{
                .index = 0,
                .count = 1,
            });
        },
        "graph frame resources should reject mismatched frame slot counts");
    require_throws(
        [&] {
            (void)frame_resources.resource_set(cubey::render::FrameSlot{
                .index = 0,
                .count = 2,
            });
        },
        "graph frame resources should reject empty but valid slots");
}

void test_render_graph_frame_resources_replace_one_slot_without_disturbing_another() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle transient =
        graph.create_buffer(buffer_desc("slot buffer"));
    graph.add_pass("simulate", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(transient)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    cubey::render::RenderGraphFrameResources frame_resources(2);
    const cubey::render::FrameSlot slot_zero{
        .index = 0,
        .count = 2,
    };
    const cubey::render::FrameSlot slot_one{
        .index = 1,
        .count = 2,
    };

    frame_resources.emplace(slot_zero, compiled)
        .bind_buffer(transient, cubey::render::RenderGraphResolvedBuffer{
                                    .buffer = buffer(0x911),
                                    .byte_size = buffer_desc("slot buffer").byte_size,
                                });
    frame_resources.emplace(slot_one, compiled)
        .bind_buffer(transient, cubey::render::RenderGraphResolvedBuffer{
                                    .buffer = buffer(0x912),
                                    .byte_size = buffer_desc("slot buffer").byte_size,
                                });
    cubey::render::RenderGraphBuilder changed_graph;
    const cubey::render::RenderGraphBufferHandle changed =
        changed_graph.create_buffer(cubey::render::RenderGraphBufferDesc{
            .label = "slot buffer",
            .byte_size = buffer_desc("slot buffer").byte_size + 4,
        });
    changed_graph.add_pass("simulate", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(changed)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph changed_compiled = changed_graph.compile();

    cubey::render::RenderGraphFrameSlotAction action =
        cubey::render::RenderGraphFrameSlotAction::Unknown;
    frame_resources.emplace(slot_zero, changed_compiled, &action)
        .bind_buffer(changed, cubey::render::RenderGraphResolvedBuffer{
                                  .buffer = buffer(0x913),
                                  .byte_size = buffer_desc("slot buffer").byte_size + 4,
                              });

    require(action == cubey::render::RenderGraphFrameSlotAction::Replaced,
            "changed graph shape should replace only the active frame slot");

    require(frame_resources.resource_set(slot_zero).buffer(transient)->buffer == buffer(0x913),
            "replacing one frame slot should update that slot");
    require(frame_resources.resource_set(slot_one).buffer(transient)->buffer == buffer(0x912),
            "replacing one frame slot should not disturb other slots");
}

void test_render_graph_frame_executor_tracks_slots_and_rejects_invalid_record_info() {
    cubey::render::RenderGraphBuilder graph;
    graph.add_pass("noop", cubey::render::RenderGraphQueueDomain::Graphics)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    cubey::render::RenderGraphFrameExecutor executor;

    require(executor.frame_slot_count() == 0, "graph frame executor should start with no slots");
    executor.resize(2);
    require(executor.frame_slot_count() == 2, "graph frame executor should report resized slots");

    const VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(0x914);
    const auto* fake_device = reinterpret_cast<const cubey::vulkan::Device*>(0x915);

    require_throws(
        [&] {
            executor.record(
                cubey::render::RenderGraphFrameRecordInfo{
                    .device = nullptr,
                    .command_buffer = command_buffer,
                    .frame_slot = {.index = 0, .count = 2},
                    .label = "test",
                },
                compiled);
        },
        "graph frame executor should require a device");
    require_throws(
        [&] {
            executor.record(
                cubey::render::RenderGraphFrameRecordInfo{
                    .device = fake_device,
                    .command_buffer = VK_NULL_HANDLE,
                    .frame_slot = {.index = 0, .count = 2},
                    .label = "test",
                },
                compiled);
        },
        "graph frame executor should require a command buffer");
    require_throws(
        [&] {
            executor.record(
                cubey::render::RenderGraphFrameRecordInfo{
                    .device = fake_device,
                    .command_buffer = command_buffer,
                    .frame_slot = {.index = 0, .count = 0},
                    .label = "test",
                },
                compiled);
        },
        "graph frame executor should reject invalid frame slots before Vulkan calls");

    executor.clear();
    require(executor.frame_slot_count() == 0,
            "cleared graph frame executor should report no slots");
}

void test_render_graph_frame_record_info_separates_command_buffer_ownership() {
    cubey::render::RenderGraphFrameRecordInfo info;
    require(info.command_buffer_mode == cubey::render::RenderGraphCommandBufferMode::BeginAndEnd,
            "render graph frame recording should own begin/end by default");

    info.command_buffer_mode = cubey::render::RenderGraphCommandBufferMode::AlreadyRecording;
    require(info.command_buffer_mode ==
                cubey::render::RenderGraphCommandBufferMode::AlreadyRecording,
            "render graph frame recording should support caller-owned command buffers");
}
