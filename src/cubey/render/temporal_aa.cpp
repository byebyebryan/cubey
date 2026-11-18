#include <cubey/render/temporal_aa.h>

#include <cubey/render/material_instance.h>
#include <cubey/render/render_graph_resolve.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/sampler.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace cubey::render {
namespace {
struct alignas(16) Uniforms {
    math::Mat4 inverse_jittered_vp{1.0F};
    math::Mat4 current_vp{1.0F};
    math::Mat4 previous_vp{1.0F};
    math::Vec4 eye{0.0F};
    math::Vec4 size{0.0F};
    math::Vec4 controls{0.0F};
};
MaterialPassInfo pass_info(bool resolve) {
    MaterialPassInfo info{.label = resolve ? "taa.resolve" : "taa.motion"};
    MaterialDescriptorSetLayout layout{.set = 0};
    const std::uint32_t count = resolve ? 6U : 3U;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto type = i == 0                     ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                          : i >= (resolve ? 4U : 2U) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                                     : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        layout.bindings.push_back(
            {.binding = i, .type = type, .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT});
    }
    info.descriptor_sets.push_back(std::move(layout));
    return info;
}
constexpr RenderGraphTextureState history_state{
    VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
RenderGraphTextureDesc desc(std::string label, VkExtent2D extent, VkFormat format) {
    return {.label = std::move(label),
            .extent = {extent.width, extent.height, 1},
            .format = format,
            .aspects = VK_IMAGE_ASPECT_COLOR_BIT};
}
} // namespace

struct TemporalAntiAliasing::Impl {
    TemporalAaConfig config;
    TemporalViewState state;
    std::array<std::optional<vulkan::Image>, 2> histories;
    vulkan::Sampler sampler;
    FrameUniformMaterialInstance<Uniforms> motion_material, resolve_material;
    std::optional<ComputePipelineResource> motion_pipeline, resolve_pipeline;
    FrameSlot slot{};
    TemporalFrame pending{};
    TemporalAaMetrics metrics{};
    bool initialized = false;
    bool motion_declared = false, resolve_declared = false, descriptors_ready = false;
    RenderGraphTextureHandle depth{}, color{}, motion{}, resolved{};

    Impl(const vulkan::Device& device, const TemporalAaConfig& cfg)
        : config(cfg), sampler(device, {.min_filter = VK_FILTER_LINEAR,
                                        .mag_filter = VK_FILTER_LINEAR,
                                        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE}),
          motion_material(device, {.material_pass = pass_info(false),
                                   .frame_slot_count = cfg.frame_slot_count}),
          resolve_material(device, {.material_pass = pass_info(true),
                                    .frame_slot_count = cfg.frame_slot_count}) {
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(device.physical_device(), &features);
        if (!features.shaderStorageImageExtendedFormats)
            throw std::runtime_error("TAA requires shaderStorageImageExtendedFormats");
        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device.physical_device(), &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device.physical_device(), &family_count,
                                                 families.data());
        if (device.queue_family() >= families.size() ||
            !(families[device.queue_family()].queueFlags & VK_QUEUE_COMPUTE_BIT))
            throw std::runtime_error("TAA requires a compute-capable submission queue");
        for (auto format : {VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT}) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(device.physical_device(), format, &properties);
            constexpr auto required =
                VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            if ((properties.optimalTilingFeatures & required) != required)
                throw std::runtime_error("TAA requires sampled/storage half-float images");
            if (format == VK_FORMAT_R16G16B16A16_SFLOAT &&
                !(properties.optimalTilingFeatures &
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
                throw std::runtime_error("TAA requires linearly filtered RGBA16F history");
        }
        for (auto& image : histories) {
            image.emplace(device,
                          vulkan::ImageConfig{.extent = {cfg.extent.width, cfg.extent.height, 1},
                                              .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                              .usage = VK_IMAGE_USAGE_STORAGE_BIT |
                                                       VK_IMAGE_USAGE_SAMPLED_BIT |
                                                       VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                              .aspect = VK_IMAGE_ASPECT_COLOR_BIT});
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device.handle(), image->handle(), &requirements);
            metrics.persistent_image_allocation_bytes += requirements.size;
        }
        emplace_single_set_compute_pipeline_resource(motion_pipeline, device,
                                                     compute_shader_file(cfg.motion_shader),
                                                     motion_material.layout());
        emplace_single_set_compute_pipeline_resource(resolve_pipeline, device,
                                                     compute_shader_file(cfg.resolve_shader),
                                                     resolve_material.layout());
        const auto pixels = std::uint64_t(cfg.extent.width) * cfg.extent.height;
        metrics.persistent_requested_bytes = pixels * 16U;
        metrics.per_slot_transient_requested_bytes = pixels * 12U;
        metrics.uniform_requested_bytes = sizeof(Uniforms) * 2U * cfg.frame_slot_count;
        for (std::uint32_t i = 0; i < cfg.frame_slot_count; ++i) {
            const FrameSlot slot{i, cfg.frame_slot_count};
            for (auto buffer : {motion_material.uniforms().buffer(slot).handle(),
                                resolve_material.uniforms().buffer(slot).handle()}) {
                VkMemoryRequirements requirements{};
                vkGetBufferMemoryRequirements(device.handle(), buffer, &requirements);
                metrics.uniform_allocation_bytes += requirements.size;
            }
        }
    }
    void check(const TemporalFrame& frame) const {
        if (!state.pending() || frame.token != pending.token)
            throw std::logic_error("TAA frame is not the pending candidate");
    }
    void dispatch(const RenderGraphExecutionContext& ctx, bool resolve) const {
        if (!descriptors_ready)
            throw std::logic_error("TAA descriptors were not prepared");
        const auto& pipeline = resolve ? *resolve_pipeline : *motion_pipeline;
        const auto set = resolve ? resolve_material.set(slot) : motion_material.set(slot);
        const auto& recorder = ctx.recorder();
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, set);
        recorder.dispatch((config.extent.width + 15U) / 16U, (config.extent.height + 15U) / 16U, 1);
    }
};

TemporalAntiAliasing::TemporalAntiAliasing() = default;
TemporalAntiAliasing::~TemporalAntiAliasing() = default;
void TemporalAntiAliasing::create(const vulkan::Device& device, const TemporalAaConfig& config) {
    if (impl_ && impl_->state.pending())
        throw std::logic_error("Submit or discard pending TAA before replacement");
    if (!config.extent.width || !config.extent.height || !config.frame_slot_count ||
        config.motion_shader.empty() || config.resolve_shader.empty() ||
        !std::isfinite(config.new_frame_weight) || config.new_frame_weight <= 0 ||
        config.new_frame_weight > 1 || !std::isfinite(config.clamping_factor) ||
        !std::isfinite(config.max_radiance) || config.max_radiance <= 0)
        throw std::invalid_argument("Invalid TAA configuration");
    auto replacement = std::make_unique<Impl>(device, config);
    impl_.swap(replacement);
}
void TemporalAntiAliasing::destroy() {
    impl_.reset();
}
bool TemporalAntiAliasing::created() const noexcept {
    return bool(impl_);
}
TemporalFrame TemporalAntiAliasing::begin_frame(FrameSlot slot, const TemporalView& view) {
    if (!impl_)
        throw std::logic_error("TAA is not created");
    validate_frame_slot(slot);
    if (!impl_)
        throw std::logic_error("TAA is not created");
    auto& p = *impl_;
    if (slot.count != p.config.frame_slot_count)
        throw std::invalid_argument("TAA slot mismatch");
    const auto frame = p.state.prepare(view, p.config.extent);
    p.slot = slot;
    p.pending = frame;
    p.motion_declared = p.resolve_declared = p.descriptors_ready = false;
    const float width = static_cast<float>(frame.extent.width);
    const float height = static_cast<float>(frame.extent.height);
    const Uniforms uniforms{frame.inverse_jittered_view_projection,
                            frame.view_projection,
                            frame.previous_view_projection,
                            math::Vec4(frame.camera_position, 0),
                            {width, height, 1.0F / width, 1.0F / height},
                            {frame.history_valid ? p.config.new_frame_weight : 1.0F,
                             p.config.clamping_factor, p.config.max_radiance, 0}};
    p.motion_material.upload(slot, uniforms);
    p.resolve_material.upload(slot, uniforms);
    return frame;
}
RenderGraphTextureHandle TemporalAntiAliasing::declare_camera_motion(RenderGraphBuilder& graph,
                                                                     RenderGraphTextureHandle depth,
                                                                     const TemporalFrame& frame) {
    if (!impl_)
        throw std::logic_error("TAA is not created");
    auto& p = *impl_;
    p.check(frame);
    if (p.motion_declared)
        throw std::logic_error("TAA motion already declared");
    p.depth = depth;
    p.motion = graph.create_texture(desc("taa.motion", frame.extent, VK_FORMAT_R16G16_SFLOAT));
    const auto buffer = graph.import_buffer({"taa.motion.uniforms", sizeof(Uniforms)},
                                            p.motion_material.uniforms().buffer(p.slot).handle());
    graph.add_pass("taa.motion", RenderGraphQueueDomain::Compute)
        .read_texture(depth, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .read_uniform_buffer(buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write_storage_texture(p.motion, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .execute([&p](const RenderGraphExecutionContext& ctx) { p.dispatch(ctx, false); });
    p.motion_declared = true;
    return p.motion;
}
RenderGraphTextureHandle TemporalAntiAliasing::declare_resolve(RenderGraphBuilder& graph,
                                                               RenderGraphTextureHandle color,
                                                               RenderGraphTextureHandle motion,
                                                               const TemporalFrame& frame) {
    if (!impl_)
        throw std::logic_error("TAA is not created");
    auto& p = *impl_;
    p.check(frame);
    if (!p.motion_declared || p.resolve_declared || motion != p.motion)
        throw std::logic_error("TAA resolve declaration order mismatch");
    std::array<RenderGraphTextureHandle, 2> history{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        history[i] = graph.import_texture(
            desc("taa.history." + std::to_string(i), frame.extent, VK_FORMAT_R16G16B16A16_SFLOAT),
            p.histories[i]->handle(), p.histories[i]->view(),
            p.initialized ? history_state : render_graph_undefined_texture_state(), history_state);
    }
    if (!p.initialized) {
        graph.add_pass("taa.initialize", RenderGraphQueueDomain::Transfer)
            .transfer_write_texture(history[0])
            .transfer_write_texture(history[1])
            .execute([history](const RenderGraphExecutionContext& ctx) {
                const VkClearColorValue zero{};
                const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                for (const auto h : history)
                    vkCmdClearColorImage(ctx.recorder().handle(), ctx.resolved_texture(h).image,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
            });
    }
    p.color = color;
    p.resolved =
        graph.create_texture(desc("taa.resolved", frame.extent, VK_FORMAT_R16G16B16A16_SFLOAT));
    const auto buffer = graph.import_buffer({"taa.resolve.uniforms", sizeof(Uniforms)},
                                            p.resolve_material.uniforms().buffer(p.slot).handle());
    // GENERAL permits the previous accepted image's write to become this read
    // without depending on CPU frame-slot or swapchain-image reuse.
    graph.add_pass("taa.resolve", RenderGraphQueueDomain::Compute)
        .read_texture(color, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .read_texture(motion, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .read_storage_texture(history[frame.history_read_index],
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write_storage_texture(history[frame.history_write_index],
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write_storage_texture(p.resolved, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .read_uniform_buffer(buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .execute([&p](const RenderGraphExecutionContext& ctx) { p.dispatch(ctx, true); });
    p.resolve_declared = true;
    return p.resolved;
}
void TemporalAntiAliasing::update_descriptors(const vulkan::Device& device,
                                              const CompiledRenderGraph& graph,
                                              const RenderGraphResourceSet& resources,
                                              const TemporalFrame& frame) {
    if (!impl_)
        throw std::logic_error("TAA is not created");
    auto& p = *impl_;
    p.check(frame);
    if (!p.resolve_declared)
        throw std::logic_error("TAA graph is incomplete");
    const auto depth = resolved_sampled_texture_view(graph, resources, p.depth);
    const auto color = resolved_sampled_texture_view(graph, resources, p.color);
    const auto motion = resources.texture(p.motion).value();
    const auto resolved = resources.texture(p.resolved).value();
    if (p.metrics.per_slot_transient_image_allocation_bytes == 0) {
        for (auto image : {motion.image, resolved.image}) {
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device.handle(), image, &requirements);
            p.metrics.per_slot_transient_image_allocation_bytes += requirements.size;
        }
    }
    MaterialDescriptorWriter(p.motion_material.set(p.slot))
        .combined_image_sampler(1, p.sampler.handle(), depth.view, depth.layout)
        .storage_image(2, motion.view)
        .update(device);
    MaterialDescriptorWriter(p.resolve_material.set(p.slot))
        .combined_image_sampler(1, p.sampler.handle(), color.view, color.layout)
        .combined_image_sampler(2, p.sampler.handle(), motion.view)
        .combined_image_sampler(3, p.sampler.handle(),
                                p.histories[frame.history_read_index]->view(),
                                VK_IMAGE_LAYOUT_GENERAL)
        .storage_image(4, resolved.view)
        .storage_image(5, p.histories[frame.history_write_index]->view())
        .update(device);
    p.descriptors_ready = true;
}
void TemporalAntiAliasing::commit_frame(std::uint64_t token, vulkan::GpuSubmissionTicket ticket) {
    if (!impl_)
        throw std::logic_error("TAA is not created");
    auto& p = *impl_;
    if (!p.resolve_declared || !p.descriptors_ready)
        throw std::logic_error("Cannot submit incomplete TAA frame");
    p.state.commit(token, ticket);
    p.initialized = true;
    p.metrics.submitted_frames = p.state.submitted_frames();
    p.metrics.history_age = p.pending.history_age;
    p.metrics.sample_offset_pixels = p.pending.sample_offset_pixels;
    p.metrics.reset_reason = p.pending.reset_reason;
}
void TemporalAntiAliasing::discard_frame() {
    if (impl_)
        impl_->state.discard();
}
void TemporalAntiAliasing::invalidate(TemporalResetReason reason) {
    if (impl_)
        impl_->state.invalidate(reason);
}
TemporalAaMetrics TemporalAntiAliasing::metrics() const {
    return impl_ ? impl_->metrics : TemporalAaMetrics{};
}
vulkan::GpuSubmissionTicket TemporalAntiAliasing::last_submission() const noexcept {
    return impl_ ? impl_->state.last_submission() : vulkan::GpuSubmissionTicket{};
}
} // namespace cubey::render
