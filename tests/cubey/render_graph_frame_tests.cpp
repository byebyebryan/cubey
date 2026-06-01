#include "render_graph_test_helpers.h"

#include <cubey/render/render_graph.h>
#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <optional>

using namespace cubey::tests::render_graph;

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
        graph.import_buffer(buffer_desc("slot buffer"), buffer(0x921));
    graph.add_pass("read", cubey::render::RenderGraphQueueDomain::Compute)
        .read_storage_buffer(buffer_handle)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});
    const cubey::render::CompiledRenderGraph compiled = graph.compile();

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

    cubey::render::RenderGraphResourceSet& second = frame_resources.emplace(slot, compiled);
    require(&first == &second, "compatible graph frame resources should reuse the slot object");
    require(!second.buffer(buffer_handle).has_value(),
            "reused graph frame resources should reset imported bindings before prepare");
    require(second.compatible(compiled),
            "reused graph frame resources should stay compatible with the compiled graph");
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
    frame_resources.emplace(slot_zero, compiled)
        .bind_buffer(transient, cubey::render::RenderGraphResolvedBuffer{
                                    .buffer = buffer(0x913),
                                    .byte_size = buffer_desc("slot buffer").byte_size,
                                });

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
