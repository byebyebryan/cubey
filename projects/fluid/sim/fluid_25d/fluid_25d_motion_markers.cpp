#include "fluid_25d_motion_markers.h"

#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/memory_barriers.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace cubey::projects::fluid::fluid_25d {
namespace {
struct UpdateParams {
    std::array<float, 4> grid_dt_cell;
    std::array<float, 4> wet_source_xy;
    std::array<std::uint32_t, 4> clock_reset;
};
struct DrawParams {
    cubey::math::Mat4 view_projection;
    std::array<float, 4> grid_cell;
    std::array<float, 4> display;
    std::array<float, 4> timing;
};
static_assert(sizeof(UpdateParams) == 48U);
static_assert(sizeof(DrawParams) == 112U);

std::filesystem::path motion_shader(const char* name) {
    return std::filesystem::path(CUBEY_FLUID_25D_SHADER_DIR) / name;
}
cubey::vulkan::DescriptorSetInfo marker_bindings(std::uint32_t count, VkShaderStageFlags stages) {
    std::vector<cubey::vulkan::DescriptorSetBindingConfig> bindings;
    for (std::uint32_t i = 0U; i < count; ++i)
        bindings.push_back(
            {.binding = i, .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .stage_flags = stages});
    return cubey::vulkan::DescriptorSetInfo(bindings);
}
} // namespace

void Fluid25DMotionMarkers::create(cubey::vulkan::Device& device, cubey::ProjectGpuServices& gpu,
                                   const Fluid25DConfig& config, Fluid25DMotionMarkerFields fields,
                                   std::array<float, 2> source_xy, std::uint32_t frame_slots,
                                   bool profile_enabled, Fluid25DMotionMarkerMode mode) {
    if (created())
        return;
    if (!fields.terrain || !fields.depth_a || !fields.depth_b || !fields.velocity ||
        !fields.source_rate || !fields.status)
        throw std::runtime_error("motion markers require all read-only hydraulic fields");
    config_ = config;
    source_xy_ = source_xy;
    mode_ = mode;
    if (profile_enabled) {
        profiler_.emplace(device, frame_slots, 17U);
        profile_frames_.resize(frame_slots, 0U);
    }
    const std::vector<Fluid25DMotionMarkerGpu> empty(kFluid25DMotionMarkerCount);
    markers_.emplace(gpu.upload_device_buffer(empty.data(), empty.size() * sizeof(empty.front()),
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              "fluid_25d motion markers"));
    const auto update_info = marker_bindings(5U, VK_SHADER_STAGE_COMPUTE_BIT);
    update_a_.emplace(device, update_info);
    update_b_.emplace(device, update_info);
    const auto render_info = marker_bindings(4U, VK_SHADER_STAGE_VERTEX_BIT);
    render_a_.emplace(device, render_info);
    render_b_.emplace(device, render_info);
    cubey::vulkan::DescriptorWriteBatch writes;
    const auto write_update = [&](VkDescriptorSet set, const cubey::vulkan::Buffer& depth) {
        writes.storage_buffer(set, 0U, depth.handle(), depth.size())
            .storage_buffer(set, 1U, fields.velocity->handle(), fields.velocity->size())
            .storage_buffer(set, 2U, fields.status->handle(), fields.status->size())
            .storage_buffer(set, 3U, buffer().handle(), buffer().size())
            .storage_buffer(set, 4U, fields.source_rate->handle(), fields.source_rate->size());
    };
    const auto write_render = [&](VkDescriptorSet set, const cubey::vulkan::Buffer& depth) {
        writes.storage_buffer(set, 0U, fields.terrain->handle(), fields.terrain->size())
            .storage_buffer(set, 1U, depth.handle(), depth.size())
            .storage_buffer(set, 2U, buffer().handle(), buffer().size())
            .storage_buffer(set, 3U, fields.status->handle(), fields.status->size());
    };
    write_update(update_a_->set(), *fields.depth_a);
    write_update(update_b_->set(), *fields.depth_b);
    write_render(render_a_->set(), *fields.depth_a);
    write_render(render_b_->set(), *fields.depth_b);
    writes.update(device);
    const std::array<VkPushConstantRange, 1> ranges{
        {{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0U, .size = sizeof(UpdateParams)}}};
    cubey::render::emplace_single_set_compute_pipeline_resource(
        update_pipeline_, device,
        cubey::render::compute_shader_file(motion_shader("fluid_25d_motion_markers.comp.spv")),
        update_a_->layout(), ranges);
}

void Fluid25DMotionMarkers::create_render_pipeline(cubey::vulkan::Device& device,
                                                   VkFormat color_format, VkFormat depth_format,
                                                   VkExtent2D extent) {
    const std::array<cubey::render::ShaderStageFile, 2> stages{
        {{VK_SHADER_STAGE_VERTEX_BIT, motion_shader("fluid_25d_motion_markers.vert.spv")},
         {VK_SHADER_STAGE_FRAGMENT_BIT, motion_shader("fluid_25d_motion_markers.frag.spv")}}};
    const std::array<VkDescriptorSetLayout, 1> layouts{render_a_->layout()};
    cubey::render::MaterialPassInfo pass{
        .label = "fluid_25d motion markers",
        .push_constants = {{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                            .offset = 0U,
                            .size = sizeof(DrawParams)}},
        .cull_mode = VK_CULL_MODE_NONE,
        .depth_test = true,
        .depth_write = false,
        .depth_compare_op = VK_COMPARE_OP_LESS_OR_EQUAL,
        .blend_enable = true,
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    };
    render_pipeline_.emplace(device, cubey::render::GraphicsPipelineFileResourceConfig{
                                         .extent = extent,
                                         .color_format = color_format,
                                         .depth_format = depth_format,
                                         .shader_stage_files = stages,
                                         .descriptor_set_layouts = layouts,
                                         .material_pass = pass,
                                     });
}
void Fluid25DMotionMarkers::destroy_render_pipeline() {
    render_pipeline_.reset();
}
void Fluid25DMotionMarkers::destroy() {
    destroy_render_pipeline();
    update_pipeline_.reset();
    render_b_.reset();
    render_a_.reset();
    update_b_.reset();
    update_a_.reset();
    markers_.reset();
    profiler_.reset();
    profile_frames_.clear();
    completed_steps_ = 0U;
}
const cubey::vulkan::Buffer& Fluid25DMotionMarkers::buffer() const {
    if (!markers_)
        throw std::runtime_error("motion marker buffer is unavailable");
    return *markers_;
}
void Fluid25DMotionMarkers::dispatch(VkCommandBuffer command_buffer, bool depth_is_a, bool reset) {
    const UpdateParams params{
        .grid_dt_cell = {static_cast<float>(config_.grid_width),
                         static_cast<float>(config_.grid_height), config_.fixed_delta_seconds,
                         config_.cell_size_m},
        .wet_source_xy = {config_.minimum_wet_depth_m, source_xy_[0], source_xy_[1], 0.0F},
        .clock_reset = {completed_steps_, reset ? 1U : 0U, static_cast<std::uint32_t>(mode_), 0U},
    };
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    cubey::render::record_compute_pipeline_dispatch(
        recorder,
        cubey::render::compute_pipeline_dispatch_info(
            *update_pipeline_, depth_is_a ? update_a_->set() : update_b_->set(),
            cubey::render::ceil_dispatch_groups(kFluid25DMotionMarkerCount, 1U, 64U)),
        VK_SHADER_STAGE_COMPUTE_BIT, params);
    cubey::vulkan::record_compute_shader_write_barrier(command_buffer);
}
void Fluid25DMotionMarkers::record_reset(VkCommandBuffer command_buffer) {
    completed_steps_ = 0U;
    dispatch(command_buffer, true, true);
}
void Fluid25DMotionMarkers::record_step(VkCommandBuffer command_buffer, bool depth_is_a) {
    if (completed_steps_ == std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("motion marker fixed-step clock overflow");
    cubey::vulkan::GpuTimestampScope timing(profiler(), command_buffer, current_slot_,
                                            "fluid_25d motion markers update");
    dispatch(command_buffer, depth_is_a, false);
    ++completed_steps_;
}
void Fluid25DMotionMarkers::begin_frame(VkCommandBuffer command_buffer, std::uint32_t slot,
                                        std::uint64_t frame) {
    current_slot_ = slot;
    if (profiler_) {
        profile_frames_.at(slot) = frame;
        profiler_->begin_frame(command_buffer, slot);
    }
}
void Fluid25DMotionMarkers::record_draw(VkCommandBuffer command_buffer, bool depth_is_a,
                                        const cubey::math::Mat4& view_projection, VkExtent2D extent,
                                        float height_scale, float interpolation) {
    cubey::vulkan::GpuTimestampScope timing(profiler(), command_buffer, current_slot_,
                                            "fluid_25d motion markers draw");
    const DrawParams params{
        .view_projection = view_projection,
        .grid_cell = {static_cast<float>(config_.grid_width),
                      static_cast<float>(config_.grid_height), config_.cell_size_m, height_scale},
        .display = {static_cast<float>(extent.width), static_cast<float>(extent.height),
                    std::clamp(interpolation, 0.0F, 1.0F), config_.minimum_wet_depth_m},
        .timing = {static_cast<float>(completed_steps_) * config_.fixed_delta_seconds,
                   config_.fixed_delta_seconds, 0.0F, 0.0F},
    };
    const cubey::vulkan::CommandRecorder recorder(command_buffer);
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, render_pipeline_->pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_GRAPHICS, render_pipeline_->layout(), 0U,
                                 depth_is_a ? render_a_->set() : render_b_->set());
    recorder.push_constants(render_pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0U, params);
    recorder.draw(kFluid25DMotionMarkerVertices, kFluid25DMotionMarkerCount);
}

void validate_fluid_25d_motion_marker_gpu_controls(cubey::vulkan::Device& device,
                                                   cubey::ProjectGpuServices& gpu) {
    Fluid25DConfig config;
    config.grid_width = 16U;
    config.grid_height = 16U;
    config.cell_size_m = 1.0F;
    config.fixed_delta_seconds = 1.0F;
    const auto run = [&](std::array<float, 2> flow, bool dry_strip, bool rejected, bool boundary) {
        std::vector<float> terrain(256U, 0.0F), depth(256U, 1.0F), source(256U, 1.0F);
        std::vector<std::array<float, 4>> velocity(256U, {flow[0], flow[1], 1.0F, 0.0F});
        if (dry_strip)
            for (std::size_t y = 0U; y < 16U; ++y)
                depth[y * 16U + 4U] = 0.0F;
        const auto upload = [&](const auto& values, const char* name) {
            return gpu.upload_device_buffer(values.data(), values.size() * sizeof(values.front()),
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            name);
        };
        auto bed = upload(terrain, "marker control bed");
        auto water = upload(depth, "marker control depth");
        auto v = upload(velocity, "marker control velocity");
        auto input = upload(source, "marker control source");
        const std::vector<std::array<std::uint32_t, 4>> flags(1U, {0U, 0U, 0U, 0U});
        auto status = upload(flags, "marker control status");
        Fluid25DMotionMarkers markers;
        markers.create(device, gpu, config, {&bed, &water, &water, &v, &input, &status},
                       {2.0F, 2.0F});
        const auto execute = [&](std::uint32_t steps, bool reset, bool reject) {
            static_cast<void>(gpu.submit_and_wait(
                {.label = "marker GPU controls",
                 .work = [&](cubey::vulkan::GpuOwnerContext& context) {
                     cubey::vulkan::ImmediateCommands commands(context);
                     if (reject) {
                         cubey::vulkan::record_memory_barrier(
                             commands.command_buffer(),
                             {.src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                              .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                              .src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                              .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT});
                         vkCmdFillBuffer(commands.command_buffer(), status.handle(), 0U,
                                         status.size(), 1U);
                         cubey::vulkan::record_transfer_write_barrier(
                             commands.command_buffer(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_ACCESS_SHADER_READ_BIT);
                     }
                     if (reset)
                         markers.record_reset(commands.command_buffer());
                     for (std::uint32_t i = 0U; i < steps; ++i)
                         markers.record_step(commands.command_buffer(), true);
                     commands.submit_and_wait();
                 }}));
        };
        const auto read = [&] {
            return gpu.readback_buffer(markers.buffer().handle(), markers.buffer().size(),
                                       "marker control readback");
        };
        execute(1U, false, false);
        const auto born = read();
        Fluid25DMotionMarkerGpu first{};
        std::memcpy(&first, born.data(), sizeof(first));
        if (first.position_age_active[3] != 1.0F)
            throw std::runtime_error("marker control failed to emit");
        execute(10U, false, rejected);
        const auto advanced = read();
        Fluid25DMotionMarkerGpu final{};
        std::memcpy(&final, advanced.data(), sizeof(final));
        if (rejected) {
            if (advanced != born)
                throw std::runtime_error("rejected marker update changed observational state");
        } else if (dry_strip || boundary) {
            if (final.position_age_active[3] != 0.0F)
                throw std::runtime_error("marker crossed dry support or crop boundary");
        } else {
            for (std::size_t axis = 0U; axis < 2U; ++axis)
                if (std::abs(final.position_age_active[axis] - first.position_age_active[axis] -
                             10.0F * flow[axis]) > 0.00001F)
                    throw std::runtime_error("uniform-flow marker displacement mismatch");
            // Reset reproduces all source releases/history bit-for-bit even
            // when the same physical steps are recorded in another grouping.
            execute(1U, true, false);
            for (std::uint32_t i = 0U; i < 10U; ++i)
                execute(1U, false, false);
            if (read() != advanced)
                throw std::runtime_error("marker reset/batch replay was not deterministic");
        }
        markers.destroy();
    };
    run({0.5F, 0.25F}, false, false, false);
    run({0.0F, 0.0F}, false, false, false);
    run({2.0F, 0.0F}, true, false, false);
    run({0.5F, 0.25F}, false, true, false);
    run({4.0F, 0.0F}, false, false, true);

    Fluid25DConfig local_config;
    local_config.grid_width = 64U;
    local_config.grid_height = 32U;
    local_config.cell_size_m = 1.0F;
    local_config.fixed_delta_seconds = 1.0F;
    local_config.minimum_wet_depth_m = 0.000001F;
    const auto run_local = [&](bool dry) {
        constexpr std::uint32_t width = 64U;
        constexpr std::uint32_t height = 32U;
        std::vector<float> terrain(width * height, 0.0F);
        std::vector<float> depth(width * height, dry ? 0.0F : 0.02F);
        std::vector<float> source(width * height, 0.0F);
        std::vector<std::array<float, 4>> velocity(width * height, {0.02F, 0.0F, 1.0F, 0.0F});
        if (!dry) {
            for (std::uint32_t y = 0U; y < height; ++y) {
                for (std::uint32_t x = 28U; x < 36U; ++x)
                    depth[static_cast<std::size_t>(y) * width + x] = 0.0F;
                for (std::uint32_t x = 40U; x < 44U; ++x)
                    depth[static_cast<std::size_t>(y) * width + x] = 0.005F;
            }
        }
        const auto upload = [&](const auto& values, const char* name) {
            return gpu.upload_device_buffer(values.data(), values.size() * sizeof(values.front()),
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            name);
        };
        auto bed = upload(terrain, "local marker control bed");
        auto water = upload(depth, "local marker control depth");
        auto v = upload(velocity, "local marker control velocity");
        auto input = upload(source, "local marker control source");
        const std::vector<std::array<std::uint32_t, 4>> flags(1U, {0U, 0U, 0U, 0U});
        auto status = upload(flags, "local marker control status");
        Fluid25DMotionMarkers markers;
        markers.create(device, gpu, local_config, {&bed, &water, &water, &v, &input, &status},
                       {2.0F, 2.0F}, 1U, false, Fluid25DMotionMarkerMode::Local);
        const auto execute = [&](std::uint32_t steps, bool reset, bool rejected) {
            static_cast<void>(gpu.submit_and_wait(
                {.label = "local marker GPU controls",
                 .work = [&](cubey::vulkan::GpuOwnerContext& context) {
                     cubey::vulkan::ImmediateCommands commands(context);
                     if (rejected) {
                         cubey::vulkan::record_memory_barrier(
                             commands.command_buffer(),
                             {.src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                              .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                              .src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                              .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT});
                         vkCmdFillBuffer(commands.command_buffer(), status.handle(), 0U,
                                         status.size(), 1U);
                         cubey::vulkan::record_transfer_write_barrier(
                             commands.command_buffer(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_ACCESS_SHADER_READ_BIT);
                     }
                     if (reset)
                         markers.record_reset(commands.command_buffer());
                     for (std::uint32_t i = 0U; i < steps; ++i)
                         markers.record_step(commands.command_buffer(), true);
                     commands.submit_and_wait();
                 }}));
        };
        const auto read = [&] {
            return gpu.readback_buffer(markers.buffer().handle(), markers.buffer().size(),
                                       "local marker control readback");
        };
        const auto active_count = [&](const auto& bytes) {
            std::uint32_t active = 0U;
            for (std::uint32_t id = 0U; id < kFluid25DMotionMarkerCount; ++id) {
                Fluid25DMotionMarkerGpu marker{};
                std::memcpy(&marker, bytes.data() + id * sizeof(marker), sizeof(marker));
                if (marker.position_age_active[3] >= 0.5F)
                    ++active;
            }
            return active;
        };

        if (dry) {
            execute(32U, false, false);
            const auto dry_state = read();
            if (active_count(dry_state) != 0U)
                throw std::runtime_error("local markers seeded on a dry field");
            execute(0U, true, false);
            const auto reset_state = read();
            if (active_count(reset_state) != 0U)
                throw std::runtime_error("local marker reset retained active parcels");
        } else {
            execute(16U, false, false);
            const auto covered = read();
            std::uint32_t left = 0U;
            std::uint32_t right = 0U;
            std::uint32_t dry_strip = 0U;
            std::uint32_t thin_film = 0U;
            std::array<bool, 16> occupied_rows{};
            for (std::uint32_t id = 0U; id < kFluid25DMotionMarkerCount; ++id) {
                Fluid25DMotionMarkerGpu marker{};
                std::memcpy(&marker, covered.data() + id * sizeof(marker), sizeof(marker));
                if (marker.position_age_active[3] < 0.5F)
                    continue;
                const float x = marker.position_age_active[0];
                const auto cell_x = static_cast<std::uint32_t>(std::floor(x + 0.5F));
                if (cell_x >= 28U && cell_x < 36U)
                    ++dry_strip;
                else if (cell_x >= 40U && cell_x < 44U)
                    ++thin_film;
                else if (cell_x < 28U)
                    ++left;
                else
                    ++right;
                occupied_rows[id / 32U] = true;
            }
            const std::uint32_t covered_rows = static_cast<std::uint32_t>(
                std::count(occupied_rows.begin(), occupied_rows.end(), true));
            if (active_count(covered) < 350U || left < 150U || right < 150U || dry_strip != 0U ||
                thin_film != 0U || covered_rows < 14U)
                throw std::runtime_error(
                    "local marker seeds did not cover material water while excluding thin film");

            Fluid25DMotionMarkerGpu before_advection{};
            std::memcpy(&before_advection, covered.data(), sizeof(before_advection));
            execute(1U, false, false);
            const auto advected = read();
            Fluid25DMotionMarkerGpu after_advection{};
            std::memcpy(&after_advection, advected.data(), sizeof(after_advection));
            if (after_advection.position_age_active[3] < 0.5F ||
                std::abs(after_advection.position_age_active[0] -
                         before_advection.position_age_active[0] - 0.02F) > 0.00001F ||
                std::abs(after_advection.position_age_active[2] -
                         before_advection.position_age_active[2] - 1.0F) > 0.00001F)
                throw std::runtime_error("local marker did not follow accepted flow on its anchor");

            execute(0U, true, false);
            const auto reset_state = read();
            if (active_count(reset_state) != 0U ||
                std::any_of(reset_state.begin(), reset_state.end(),
                            [](std::uint8_t byte) { return byte != 0U; }))
                throw std::runtime_error("local marker reset did not clear exact state bytes");
            execute(16U, false, false);
            const auto replayed = read();
            if (replayed != covered)
                throw std::runtime_error("local marker reset replay was not deterministic");
            execute(1U, false, true);
            if (read() != replayed)
                throw std::runtime_error("rejected local marker update changed state bytes");
        }
        markers.destroy();
    };
    run_local(false);
    run_local(true);

    Fluid25DConfig channel_config;
    channel_config.grid_width = 256U;
    channel_config.grid_height = 128U;
    channel_config.cell_size_m = 1.0F;
    channel_config.fixed_delta_seconds = 1.0F;
    channel_config.minimum_wet_depth_m = 0.000001F;
    constexpr std::uint32_t channel_width = 256U;
    constexpr std::uint32_t channel_height = 128U;
    std::vector<float> channel_terrain(channel_width * channel_height, 0.0F);
    std::vector<float> channel_depth(channel_width * channel_height, 0.0F);
    std::vector<float> channel_source(channel_width * channel_height, 0.0F);
    std::vector<std::array<float, 4>> channel_velocity(channel_width * channel_height,
                                                       {0.0F, 0.0F, 1.0F, 0.0F});
    // At this 256x128 resolution, the nine probes round to relative x
    // columns 1, 3/4, or 6 in each eight-cell anchor tile. This thin channel
    // is in the unprobed relative column 2, so only the bounded global search
    // can seed it.
    for (std::uint32_t y = 24U; y < 104U; ++y)
        channel_depth[static_cast<std::size_t>(y) * channel_width + 18U] = 1.0F;
    const auto upload_channel = [&](const auto& values, const char* name) {
        return gpu.upload_device_buffer(values.data(), values.size() * sizeof(values.front()),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        name);
    };
    auto channel_bed = upload_channel(channel_terrain, "thin channel marker control bed");
    auto channel_water = upload_channel(channel_depth, "thin channel marker control depth");
    auto channel_v = upload_channel(channel_velocity, "thin channel marker control velocity");
    auto channel_input = upload_channel(channel_source, "thin channel marker control source");
    const std::vector<std::array<std::uint32_t, 4>> channel_flags(1U, {0U, 0U, 0U, 0U});
    auto channel_status = upload_channel(channel_flags, "thin channel marker control status");
    Fluid25DMotionMarkers channel_markers;
    channel_markers.create(
        device, gpu, channel_config,
        {&channel_bed, &channel_water, &channel_water, &channel_v, &channel_input, &channel_status},
        {2.0F, 2.0F}, 1U, false, Fluid25DMotionMarkerMode::Local);
    const auto execute_channel = [&](std::uint32_t steps, bool reset, bool rejected) {
        static_cast<void>(gpu.submit_and_wait(
            {.label = "thin channel marker GPU controls",
             .work = [&](cubey::vulkan::GpuOwnerContext& context) {
                 cubey::vulkan::ImmediateCommands commands(context);
                 if (rejected) {
                     cubey::vulkan::record_memory_barrier(
                         commands.command_buffer(),
                         {.src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                          .src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                          .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT});
                     vkCmdFillBuffer(commands.command_buffer(), channel_status.handle(), 0U,
                                     channel_status.size(), 1U);
                     cubey::vulkan::record_transfer_write_barrier(
                         commands.command_buffer(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_ACCESS_SHADER_READ_BIT);
                 }
                 if (reset)
                     channel_markers.record_reset(commands.command_buffer());
                 for (std::uint32_t i = 0U; i < steps; ++i)
                     channel_markers.record_step(commands.command_buffer(), true);
                 commands.submit_and_wait();
             }}));
    };
    const auto read_channel = [&] {
        return gpu.readback_buffer(channel_markers.buffer().handle(),
                                   channel_markers.buffer().size(),
                                   "thin channel marker control readback");
    };
    execute_channel(16U, false, false);
    const auto channel_state = read_channel();
    std::uint32_t channel_active = 0U;
    std::uint32_t channel_min_y = channel_height;
    std::uint32_t channel_max_y = 0U;
    std::array<bool, channel_height> channel_rows{};
    for (std::uint32_t id = 0U; id < kFluid25DMotionMarkerCount; ++id) {
        Fluid25DMotionMarkerGpu marker{};
        std::memcpy(&marker, channel_state.data() + id * sizeof(marker), sizeof(marker));
        if (marker.position_age_active[3] < 0.5F)
            continue;
        const auto cell_x =
            static_cast<std::uint32_t>(std::floor(marker.position_age_active[0] + 0.5F));
        const auto cell_y =
            static_cast<std::uint32_t>(std::floor(marker.position_age_active[1] + 0.5F));
        if (cell_x != 18U || cell_y < 24U || cell_y >= 104U)
            throw std::runtime_error("thin channel marker escaped its only wet support");
        ++channel_active;
        channel_min_y = std::min(channel_min_y, cell_y);
        channel_max_y = std::max(channel_max_y, cell_y);
        channel_rows[cell_y] = true;
    }
    const std::uint32_t channel_row_coverage =
        static_cast<std::uint32_t>(std::count(channel_rows.begin(), channel_rows.end(), true));
    if (channel_active < 8U || channel_row_coverage < 12U || channel_max_y - channel_min_y < 32U)
        throw std::runtime_error("bounded global fallback could not reach the thin wet channel");

    execute_channel(0U, true, false);
    const auto channel_reset = read_channel();
    if (std::any_of(channel_reset.begin(), channel_reset.end(),
                    [](std::uint8_t byte) { return byte != 0U; }))
        throw std::runtime_error("thin channel marker reset did not clear exact state bytes");
    execute_channel(16U, false, false);
    const auto channel_replay = read_channel();
    if (channel_replay != channel_state)
        throw std::runtime_error("thin channel fallback replay was not deterministic");
    execute_channel(1U, false, true);
    if (read_channel() != channel_replay)
        throw std::runtime_error("rejected thin channel marker update changed state bytes");
    channel_markers.destroy();
    std::printf("fluid_25d: motion marker GPU controls PASS "
                "(source uniform/rest/dry/rejection/boundary/reset/batching; local coverage/dry/"
                "rejection/reset/replay/flow/thin-channel-fallback)\n");
}

} // namespace cubey::projects::fluid::fluid_25d
