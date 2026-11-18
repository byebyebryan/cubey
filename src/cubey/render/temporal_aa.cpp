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
#include <vector>

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
bool matches_extent(const RenderGraphTextureDesc& texture, VkExtent2D extent) {
    return texture.extent.width == extent.width && texture.extent.height == extent.height &&
           texture.extent.depth == 1;
}
} // namespace

std::string_view temporal_aa_unsupported_reason(const vulkan::Device& device) {
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device.physical_device(), &features);
    if (!features.shaderStorageImageExtendedFormats)
        return "TAA requires shaderStorageImageExtendedFormats";
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device.physical_device(), &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device.physical_device(), &family_count, families.data());
    if (device.queue_family() >= family_count ||
        !(families[device.queue_family()].queueFlags & VK_QUEUE_COMPUTE_BIT))
        return "TAA requires a compute-capable submission queue";
    for (auto format : {VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device.physical_device(), format, &properties);
        constexpr auto required =
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & required) != required)
            return "TAA requires sampled/storage half-float images";
        if (format == VK_FORMAT_R16G16B16A16_SFLOAT &&
            !(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            return "TAA requires linearly filtered RGBA16F history";
    }
    return {};
}

struct TemporalCameraMotion::Impl {
    const std::uint32_t frame_slot_count;
    vulkan::Sampler sampler;
    FrameUniformMaterialInstance<Uniforms> material;
    std::optional<ComputePipelineResource> pipeline;
    std::vector<std::uint8_t> declared_slots;
    std::vector<std::uint8_t> descriptors_ready_slots;
    std::vector<VkExtent2D> declared_extents;
    std::uint64_t uniform_allocation_bytes = 0;

    Impl(const vulkan::Device& device, std::uint32_t slots,
         const std::filesystem::path& motion_shader)
        : frame_slot_count(slots),
          sampler(device, {.min_filter = VK_FILTER_LINEAR,
                           .mag_filter = VK_FILTER_LINEAR,
                           .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE}),
          material(device, FrameUniformMaterialInstanceConfig{.material_pass = pass_info(false),
                                                              .frame_slot_count = slots}),
          declared_slots(slots), descriptors_ready_slots(slots), declared_extents(slots) {
        if (motion_shader.empty())
            throw std::invalid_argument("Camera motion requires a compute shader");
        emplace_single_set_compute_pipeline_resource(
            pipeline, device, compute_shader_file(motion_shader), material.layout());
        for (std::uint32_t i = 0; i < frame_slot_count; ++i) {
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(
                device.handle(), material.uniforms().buffer({i, frame_slot_count}).handle(),
                &requirements);
            uniform_allocation_bytes += requirements.size;
        }
    }

    void dispatch(FrameSlot slot, VkExtent2D extent,
                  const RenderGraphExecutionContext& context) const {
        if (!descriptors_ready_slots.at(slot.index))
            throw std::logic_error("Camera motion descriptors were not prepared");
        const auto& recorder = context.recorder();
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout(), 0,
                                     material.set(slot));
        recorder.dispatch((extent.width + 15U) / 16U, (extent.height + 15U) / 16U, 1);
    }
};

TemporalCameraMotion::TemporalCameraMotion(const vulkan::Device& device,
                                           std::uint32_t frame_slot_count,
                                           const std::filesystem::path& motion_shader)
    : impl_(std::make_unique<Impl>(device, frame_slot_count, motion_shader)) {}
TemporalCameraMotion::~TemporalCameraMotion() = default;
RenderGraphTextureHandle TemporalCameraMotion::declare(RenderGraphBuilder& graph, FrameSlot slot,
                                                       RenderGraphTextureHandle depth,
                                                       const TemporalFrame& frame) {
    if (!impl_)
        throw std::logic_error("Camera motion is not created");
    auto& p = *impl_;
    validate_frame_slot(slot);
    if (slot.count != p.frame_slot_count)
        throw std::invalid_argument("Camera motion slot mismatch");
    if (!frame.extent.width || !frame.extent.height)
        throw std::invalid_argument("Camera motion requires a non-empty extent");

    const float width = static_cast<float>(frame.extent.width);
    const float height = static_cast<float>(frame.extent.height);
    const Uniforms uniforms{frame.inverse_jittered_view_projection,
                            frame.view_projection,
                            frame.previous_view_projection,
                            math::Vec4(frame.camera_position, 0),
                            {width, height, 1.0F / width, 1.0F / height},
                            {0.0F, 0.0F, 0.0F, 0.0F}};
    p.material.upload(slot, uniforms);

    const auto motion =
        graph.create_texture(desc("taa.motion", frame.extent, VK_FORMAT_R16G16_SFLOAT));
    const auto buffer = graph.import_buffer({"taa.motion.uniforms", sizeof(Uniforms)},
                                            p.material.uniforms().buffer(slot).handle());
    p.descriptors_ready_slots[slot.index] = 0;
    p.declared_slots[slot.index] = 1;
    p.declared_extents[slot.index] = frame.extent;
    graph.add_pass("taa.motion", RenderGraphQueueDomain::Compute)
        .read_texture(depth, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .read_uniform_buffer(buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write_storage_texture(motion, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .execute([&p, slot, extent = frame.extent](const RenderGraphExecutionContext& context) {
            p.dispatch(slot, extent, context);
        });
    return motion;
}
void TemporalCameraMotion::update_descriptors(const vulkan::Device& device, FrameSlot slot,
                                              const CompiledRenderGraph& graph,
                                              const RenderGraphResourceSet& resources,
                                              RenderGraphTextureHandle depth,
                                              RenderGraphTextureHandle motion) {
    if (!impl_)
        throw std::logic_error("Camera motion is not created");
    auto& p = *impl_;
    validate_frame_slot(slot);
    if (slot.count != p.frame_slot_count)
        throw std::invalid_argument("Camera motion slot mismatch");
    if (!p.declared_slots[slot.index])
        throw std::logic_error("Camera motion graph was not declared for this slot");

    const auto& depth_description = graph.texture(depth).desc;
    const auto& motion_description = graph.texture(motion).desc;
    const auto extent = p.declared_extents[slot.index];
    if (!matches_extent(depth_description, extent))
        throw std::invalid_argument("Camera motion requires a native-size depth image");
    if (motion_description.format != VK_FORMAT_R16G16_SFLOAT ||
        !matches_extent(motion_description, extent))
        throw std::invalid_argument("Camera motion requires a native-size RG16F image");

    const auto depth_view = resolved_sampled_texture_view(graph, resources, depth);
    const auto motion_view = resources.texture(motion).value();
    MaterialDescriptorWriter(p.material.set(slot))
        .combined_image_sampler(1, p.sampler.handle(), depth_view.view, depth_view.layout)
        .storage_image(2, motion_view.view)
        .update(device);
    p.descriptors_ready_slots[slot.index] = 1;
}
std::uint64_t TemporalCameraMotion::uniform_requested_bytes() const noexcept {
    return impl_ ? sizeof(Uniforms) * impl_->frame_slot_count : 0;
}
std::uint64_t TemporalCameraMotion::uniform_allocation_bytes() const noexcept {
    return impl_ ? impl_->uniform_allocation_bytes : 0;
}

struct TemporalAntiAliasing::Impl {
    TemporalAaConfig config;
    TemporalViewState state;
    std::array<std::optional<vulkan::Image>, 2> histories;
    vulkan::Sampler sampler;
    std::unique_ptr<TemporalCameraMotion> camera_motion;
    FrameUniformMaterialInstance<Uniforms> resolve_material;
    std::optional<ComputePipelineResource> resolve_pipeline;
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
          resolve_material(device, {.material_pass = pass_info(true),
                                    .frame_slot_count = cfg.frame_slot_count}) {
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
        if (!cfg.motion_shader.empty()) {
            camera_motion = std::make_unique<TemporalCameraMotion>(device, cfg.frame_slot_count,
                                                                   cfg.motion_shader);
        }
        emplace_single_set_compute_pipeline_resource(resolve_pipeline, device,
                                                     compute_shader_file(cfg.resolve_shader),
                                                     resolve_material.layout());
        const auto pixels = std::uint64_t(cfg.extent.width) * cfg.extent.height;
        metrics.persistent_requested_bytes = pixels * 16U;
        metrics.per_slot_transient_requested_bytes = pixels * (camera_motion ? 12U : 8U);
        const auto account_uniforms = [&](const auto& material) {
            metrics.uniform_requested_bytes += sizeof(Uniforms) * cfg.frame_slot_count;
            for (std::uint32_t i = 0; i < cfg.frame_slot_count; ++i) {
                VkMemoryRequirements requirements{};
                vkGetBufferMemoryRequirements(
                    device.handle(), material.uniforms().buffer({i, cfg.frame_slot_count}).handle(),
                    &requirements);
                metrics.uniform_allocation_bytes += requirements.size;
            }
        };
        account_uniforms(resolve_material);
        if (camera_motion) {
            metrics.uniform_requested_bytes += camera_motion->uniform_requested_bytes();
            metrics.uniform_allocation_bytes += camera_motion->uniform_allocation_bytes();
        }
    }
    void check(const TemporalFrame& frame) const {
        if (!state.pending() || frame.token != pending.token)
            throw std::logic_error("TAA frame is not the pending candidate");
    }
    void dispatch(const RenderGraphExecutionContext& ctx) const {
        if (!descriptors_ready)
            throw std::logic_error("TAA descriptors were not prepared");
        const auto& pipeline = *resolve_pipeline;
        const auto set = resolve_material.set(slot);
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
        config.resolve_shader.empty() || !std::isfinite(config.new_frame_weight) ||
        config.new_frame_weight <= 0 || config.new_frame_weight > 1 ||
        !std::isfinite(config.clamping_factor) || !std::isfinite(config.max_radiance) ||
        config.max_radiance <= 0)
        throw std::invalid_argument("Invalid TAA configuration");
    if (const auto reason = temporal_aa_unsupported_reason(device); !reason.empty())
        throw std::runtime_error(std::string(reason));
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
    if (!p.camera_motion || p.motion_declared || p.resolve_declared)
        throw std::logic_error("TAA camera motion is unavailable or already declared");
    p.depth = depth;
    p.motion = p.camera_motion->declare(graph, p.slot, depth, frame);
    p.motion_declared = true;
    return p.motion;
}
RenderGraphTextureHandle TemporalAntiAliasing::declare_resolve(RenderGraphBuilder& graph,
                                                               RenderGraphTextureHandle color,
                                                               RenderGraphTextureHandle motion,
                                                               const TemporalFrame& frame) {
    return declare_resolve_impl(graph, color, motion, frame, false);
}
RenderGraphTextureHandle TemporalAntiAliasing::declare_resolve_with_motion(
    RenderGraphBuilder& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle motion,
    const TemporalFrame& frame) {
    return declare_resolve_impl(graph, color, motion, frame, true);
}
RenderGraphTextureHandle TemporalAntiAliasing::declare_resolve_impl(RenderGraphBuilder& graph,
                                                                    RenderGraphTextureHandle color,
                                                                    RenderGraphTextureHandle motion,
                                                                    const TemporalFrame& frame,
                                                                    bool producer_motion) {
    if (!impl_)
        throw std::logic_error("TAA is not created");
    auto& p = *impl_;
    p.check(frame);
    if (p.resolve_declared || (producer_motion ? static_cast<bool>(p.camera_motion)
                                               : !p.motion_declared || motion != p.motion))
        throw std::logic_error("TAA resolve declaration order mismatch");
    p.motion = motion;
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
        .execute([&p](const RenderGraphExecutionContext& ctx) { p.dispatch(ctx); });
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
    const auto& color_description = graph.texture(p.color).desc;
    if (!matches_extent(color_description, frame.extent) ||
        color_description.aspects != VK_IMAGE_ASPECT_COLOR_BIT)
        throw std::invalid_argument("TAA requires a native-size color image");
    const auto& motion_description = graph.texture(p.motion).desc;
    if (motion_description.format != VK_FORMAT_R16G16_SFLOAT ||
        !matches_extent(motion_description, frame.extent))
        throw std::invalid_argument("TAA requires a native-size RG16F motion image");
    if (p.motion_declared && !matches_extent(graph.texture(p.depth).desc, frame.extent))
        throw std::invalid_argument("TAA requires a native-size depth image");
    if (p.motion_declared)
        p.camera_motion->update_descriptors(device, p.slot, graph, resources, p.depth, p.motion);
    const auto color = resolved_sampled_texture_view(graph, resources, p.color);
    const auto motion = resolved_sampled_texture_view(graph, resources, p.motion);
    const auto resolved = resources.texture(p.resolved).value();
    if (p.metrics.per_slot_transient_image_allocation_bytes == 0) {
        const auto account = [&](VkImage image) {
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device.handle(), image, &requirements);
            p.metrics.per_slot_transient_image_allocation_bytes += requirements.size;
        };
        account(resolved.image);
        if (p.motion_declared)
            account(motion.image);
    }
    MaterialDescriptorWriter(p.resolve_material.set(p.slot))
        .combined_image_sampler(1, p.sampler.handle(), color.view, color.layout)
        .combined_image_sampler(2, p.sampler.handle(), motion.view, motion.layout)
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
