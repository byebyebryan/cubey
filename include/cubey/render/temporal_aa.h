#pragma once

#include <cubey/core/math.h>
#include <cubey/render/frame_data.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/submission_tickets.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace cubey::render {

enum class AntiAliasingMode : std::uint8_t {
    Off,
    Taa
};

[[nodiscard]] std::string_view anti_aliasing_mode_name(AntiAliasingMode mode);
[[nodiscard]] AntiAliasingMode anti_aliasing_mode_from_name(std::string_view name);
// Empty when the device and its selected submission queue support the native resolve.
[[nodiscard]] std::string_view temporal_aa_unsupported_reason(const vulkan::Device& device);

enum class TemporalResetReason : std::uint8_t {
    None,
    FirstFrame,
    Explicit,
    TargetChanged,
    ViewChanged,
    ContentChanged,
    ProjectionChanged,
    DiscardedFrame,
};

[[nodiscard]] std::string_view temporal_reset_reason_name(TemporalResetReason reason);

// Positive offsets describe the location sampled in the unjittered image:
// raster pixel p samples p + sample_offset. This shifts projected geometry by
// the negative offset, with positive screen Y pointing down.
[[nodiscard]] math::Vec2 temporal_sample_offset(std::uint64_t phase);
[[nodiscard]] math::Mat4 temporal_jitter_projection(const math::Mat4& projection,
                                                    math::Vec2 sample_offset, VkExtent2D extent);
[[nodiscard]] math::Vec2 temporal_screen_position(math::Vec4 clip_position, VkExtent2D extent);
[[nodiscard]] math::Vec2 temporal_motion_pixels(math::Vec3 current_world, math::Vec3 previous_world,
                                                const math::Mat4& current_view_projection,
                                                const math::Mat4& previous_view_projection,
                                                VkExtent2D extent);
// Zero is valid stationary motion. Invalid/unknown previous surfaces use a finite
// vector outside the previous image, so the resolve rejects their history.
[[nodiscard]] math::Vec2 temporal_invalid_motion(VkExtent2D extent);

struct TemporalView {
    math::Mat4 projection{1.0F};
    math::Mat4 view{1.0F};
    std::uint64_t view_id = 1;
    std::uint64_t content_generation = 0;
    bool reset = false;
};

struct TemporalFrame {
    std::uint64_t token = 0;
    std::uint64_t sequence = 0;
    std::uint32_t history_age = 0;
    std::uint32_t history_read_index = 1;
    std::uint32_t history_write_index = 0;
    VkExtent2D extent{};
    math::Vec2 sample_offset_pixels{0.0F};
    math::Mat4 jittered_projection{1.0F};
    math::Mat4 jittered_view_projection{1.0F};
    math::Mat4 inverse_jittered_view_projection{1.0F};
    math::Mat4 view_projection{1.0F};
    math::Mat4 previous_view_projection{1.0F};
    math::Vec3 camera_position{0.0F};
    TemporalResetReason reset_reason = TemporalResetReason::FirstFrame;
    bool history_valid = false;
};

// One state per independently rendered view. Preparing/recording does not make
// an image history. commit() is called only after its queue submission succeeds.
// A discarded attempt neither advances jitter nor publishes its view matrices.
class TemporalViewState {
  public:
    [[nodiscard]] TemporalFrame prepare(const TemporalView& view, VkExtent2D extent);
    void commit(std::uint64_t token, vulkan::GpuSubmissionTicket ticket);
    void discard();
    void invalidate(TemporalResetReason reason = TemporalResetReason::Explicit);

    [[nodiscard]] bool pending() const noexcept;
    [[nodiscard]] std::uint64_t submitted_frames() const noexcept;
    [[nodiscard]] vulkan::GpuSubmissionTicket last_submission() const noexcept;

  private:
    struct Accepted {
        TemporalView view{};
        TemporalFrame frame{};
    };
    struct Pending {
        TemporalView view{};
        TemporalFrame frame{};
    };

    std::optional<Accepted> accepted_;
    std::optional<Pending> pending_;
    TemporalResetReason invalidation_ = TemporalResetReason::None;
    vulkan::GpuSubmissionTicket last_submission_{};
    std::uint64_t next_token_ = 1;
    std::uint64_t submitted_frames_ = 0;
};

struct TemporalAaConfig {
    VkExtent2D extent{};
    std::uint32_t frame_slot_count = 1;
    // Leave empty when the producer supplies motion directly.
    std::filesystem::path motion_shader;
    std::filesystem::path resolve_shader;
    float new_frame_weight = 0.1F;
    float clamping_factor = 1.0F;
    float max_radiance = 10000.0F;
};

// Builds the shared camera-motion field from scene depth and a prepared temporal
// frame. The caller owns temporal history and must update descriptors after graph
// compilation, while using a retired frame slot.
class TemporalCameraMotion {
  public:
    TemporalCameraMotion(const vulkan::Device& device, std::uint32_t frame_slot_count,
                         const std::filesystem::path& motion_shader);
    ~TemporalCameraMotion();
    TemporalCameraMotion(const TemporalCameraMotion&) = delete;
    TemporalCameraMotion& operator=(const TemporalCameraMotion&) = delete;

    [[nodiscard]] RenderGraphTextureHandle declare(RenderGraphBuilder& graph, FrameSlot slot,
                                                   RenderGraphTextureHandle depth,
                                                   const TemporalFrame& frame);
    void update_descriptors(const vulkan::Device& device, FrameSlot slot,
                            const CompiledRenderGraph& graph,
                            const RenderGraphResourceSet& resources, RenderGraphTextureHandle depth,
                            RenderGraphTextureHandle motion);

    // Used by TAA's existing native-uniform accounting.
    [[nodiscard]] std::uint64_t uniform_requested_bytes() const noexcept;
    [[nodiscard]] std::uint64_t uniform_allocation_bytes() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct TemporalAaMetrics {
    std::uint64_t submitted_frames = 0;
    std::uint32_t history_age = 0;
    TemporalResetReason reset_reason = TemporalResetReason::FirstFrame;
    math::Vec2 sample_offset_pixels{0.0F};
    std::uint64_t persistent_requested_bytes = 0;
    std::uint64_t per_slot_transient_requested_bytes = 0;
    std::uint64_t uniform_requested_bytes = 0;
    std::uint64_t persistent_image_allocation_bytes = 0;
    std::uint64_t per_slot_transient_image_allocation_bytes = 0;
    std::uint64_t uniform_allocation_bytes = 0;
};

// Depth uses Vulkan forward Z [0,1], with clear/miss=1. Motion is previous minus
// current unjittered pixel position, positive Y down. One runtime owns one view.
// Resource creation/replacement/destruction requires the caller to retire the
// last real GPU use. Descriptor/uniform slots follow the host's retired frame
// slots; the two history images follow submitted view sequence instead.
class TemporalAntiAliasing {
  public:
    TemporalAntiAliasing();
    ~TemporalAntiAliasing();
    TemporalAntiAliasing(const TemporalAntiAliasing&) = delete;
    TemporalAntiAliasing& operator=(const TemporalAntiAliasing&) = delete;
    TemporalAntiAliasing(TemporalAntiAliasing&&) = delete;
    TemporalAntiAliasing& operator=(TemporalAntiAliasing&&) = delete;

    // Strong replacement guarantee: construction failure preserves the previous
    // complete generation. Shader files and image formats are checked on demand.
    void create(const vulkan::Device& device, const TemporalAaConfig& config);
    void destroy();
    [[nodiscard]] bool created() const noexcept;

    [[nodiscard]] TemporalFrame begin_frame(FrameSlot slot, const TemporalView& view);
    [[nodiscard]] RenderGraphTextureHandle declare_camera_motion(RenderGraphBuilder& graph,
                                                                 RenderGraphTextureHandle depth,
                                                                 const TemporalFrame& frame);
    // An engine may overlay rigid-object motion into this same motion target
    // before declaring the resolve. Color/depth/motion must match the view.
    [[nodiscard]] RenderGraphTextureHandle declare_resolve(RenderGraphBuilder& graph,
                                                           RenderGraphTextureHandle color,
                                                           RenderGraphTextureHandle motion,
                                                           const TemporalFrame& frame);
    // Motion must be native-size RG16F in the same pixel convention as camera
    // motion. The producer owns the image and declares or imports its writes.
    [[nodiscard]] RenderGraphTextureHandle
    declare_resolve_with_motion(RenderGraphBuilder& graph, RenderGraphTextureHandle color,
                                RenderGraphTextureHandle motion, const TemporalFrame& frame);
    void update_descriptors(const vulkan::Device& device, const CompiledRenderGraph& graph,
                            const RenderGraphResourceSet& resources, const TemporalFrame& frame);
    void commit_frame(std::uint64_t token, vulkan::GpuSubmissionTicket ticket);
    void discard_frame();
    void invalidate(TemporalResetReason reason = TemporalResetReason::Explicit);
    [[nodiscard]] TemporalAaMetrics metrics() const;
    [[nodiscard]] vulkan::GpuSubmissionTicket last_submission() const noexcept;

  private:
    [[nodiscard]] RenderGraphTextureHandle declare_resolve_impl(RenderGraphBuilder& graph,
                                                                RenderGraphTextureHandle color,
                                                                RenderGraphTextureHandle motion,
                                                                const TemporalFrame& frame,
                                                                bool producer_motion);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cubey::render
