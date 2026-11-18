#include <cubey/render/temporal_aa.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cubey::render {
math::Vec2 temporal_invalid_motion(VkExtent2D extent) {
    return {static_cast<float>(extent.width) + 16.0F, static_cast<float>(extent.height) + 16.0F};
}
namespace {

void validate_extent(VkExtent2D extent) {
    if (extent.width == 0U || extent.height == 0U) {
        throw std::invalid_argument("temporal view requires a positive extent");
    }
}

[[nodiscard]] bool finite(const math::Mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool equal(const math::Mat4& lhs, const math::Mat4& rhs) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (lhs[column][row] != rhs[column][row]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] float radical_inverse(unsigned base, unsigned index) {
    float value = 0.0F;
    float denominator = 1.0F;
    while (index != 0U) {
        denominator *= static_cast<float>(base);
        value += static_cast<float>(index % base) / denominator;
        index /= base;
    }
    return value;
}

} // namespace

std::string_view anti_aliasing_mode_name(AntiAliasingMode mode) {
    switch (mode) {
    case AntiAliasingMode::Off:
        return "off";
    case AntiAliasingMode::Taa:
        return "taa";
    }
    throw std::invalid_argument("invalid anti-aliasing mode");
}

AntiAliasingMode anti_aliasing_mode_from_name(std::string_view name) {
    if (name == "off") {
        return AntiAliasingMode::Off;
    }
    if (name == "taa") {
        return AntiAliasingMode::Taa;
    }
    throw std::invalid_argument("anti-aliasing must be off or taa");
}

std::string_view temporal_reset_reason_name(TemporalResetReason reason) {
    switch (reason) {
    case TemporalResetReason::None:
        return "none";
    case TemporalResetReason::FirstFrame:
        return "first-frame";
    case TemporalResetReason::Explicit:
        return "explicit";
    case TemporalResetReason::TargetChanged:
        return "target-changed";
    case TemporalResetReason::ViewChanged:
        return "view-changed";
    case TemporalResetReason::ContentChanged:
        return "content-changed";
    case TemporalResetReason::ProjectionChanged:
        return "projection-changed";
    case TemporalResetReason::DiscardedFrame:
        return "discarded-frame";
    }
    throw std::invalid_argument("invalid temporal reset reason");
}

math::Vec2 temporal_sample_offset(std::uint64_t phase) {
    const auto index = static_cast<unsigned>((phase % 16U) + 1U);
    return math::Vec2{radical_inverse(2U, index), radical_inverse(3U, index)} - 0.5F;
}

math::Mat4 temporal_jitter_projection(const math::Mat4& projection, math::Vec2 sample_offset,
                                      VkExtent2D extent) {
    validate_extent(extent);
    if (!finite(projection) || !std::isfinite(sample_offset.x) || !std::isfinite(sample_offset.y) ||
        std::abs(sample_offset.x) > 0.5F || std::abs(sample_offset.y) > 0.5F) {
        throw std::invalid_argument("temporal projection requires finite subpixel offsets");
    }
    math::Mat4 shift{1.0F};
    shift[3][0] = -2.0F * sample_offset.x / static_cast<float>(extent.width);
    shift[3][1] = -2.0F * sample_offset.y / static_cast<float>(extent.height);
    return shift * projection;
}

math::Vec2 temporal_screen_position(math::Vec4 clip_position, VkExtent2D extent) {
    validate_extent(extent);
    if (!std::isfinite(clip_position.x) || !std::isfinite(clip_position.y) ||
        !std::isfinite(clip_position.w) || clip_position.w <= 0.0F) {
        throw std::invalid_argument("temporal position has no valid forward projection");
    }
    return (math::Vec2{clip_position} / clip_position.w * 0.5F + 0.5F) *
           math::Vec2{extent.width, extent.height};
}

math::Vec2 temporal_motion_pixels(math::Vec3 current_world, math::Vec3 previous_world,
                                  const math::Mat4& current_view_projection,
                                  const math::Mat4& previous_view_projection, VkExtent2D extent) {
    return temporal_screen_position(previous_view_projection * math::Vec4{previous_world, 1.0F},
                                    extent) -
           temporal_screen_position(current_view_projection * math::Vec4{current_world, 1.0F},
                                    extent);
}

TemporalFrame TemporalViewState::prepare(const TemporalView& view, VkExtent2D extent) {
    validate_extent(extent);
    if (pending_) {
        throw std::logic_error("submit or discard the pending temporal frame before preparing");
    }
    if (view.view_id == 0U || !finite(view.projection) || !finite(view.view) ||
        !finite(glm::inverse(view.projection)) || !finite(glm::inverse(view.view))) {
        throw std::invalid_argument(
            "temporal view requires finite invertible transforms and identity");
    }
    if (next_token_ == std::numeric_limits<std::uint64_t>::max() ||
        submitted_frames_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("temporal frame sequence exhausted");
    }

    TemporalResetReason reason = invalidation_;
    if (reason == TemporalResetReason::None) {
        if (!accepted_) {
            reason = TemporalResetReason::FirstFrame;
        } else if (accepted_->frame.extent.width != extent.width ||
                   accepted_->frame.extent.height != extent.height) {
            reason = TemporalResetReason::TargetChanged;
        } else if (view.view_id != accepted_->view.view_id) {
            reason = TemporalResetReason::ViewChanged;
        } else if (view.content_generation != accepted_->view.content_generation) {
            reason = TemporalResetReason::ContentChanged;
        } else if (!equal(view.projection, accepted_->view.projection)) {
            reason = TemporalResetReason::ProjectionChanged;
        } else if (view.reset) {
            reason = TemporalResetReason::Explicit;
        }
    }
    const bool valid = reason == TemporalResetReason::None;
    const auto age = valid
                         ? std::min<std::uint32_t>(accepted_->frame.history_age,
                                                   std::numeric_limits<std::uint32_t>::max() - 1U) +
                               1U
                         : 0U;
    TemporalFrame frame{
        .token = next_token_++,
        .sequence = submitted_frames_,
        .history_age = age,
        .history_read_index = accepted_ ? accepted_->frame.history_write_index : 1U,
        .history_write_index = accepted_ ? 1U - accepted_->frame.history_write_index : 0U,
        .extent = extent,
        .sample_offset_pixels = temporal_sample_offset(age),
        .view_projection = view.projection * view.view,
        .previous_view_projection =
            valid ? accepted_->frame.view_projection : view.projection * view.view,
        .camera_position = math::Vec3{glm::inverse(view.view)[3]},
        .reset_reason = reason,
        .history_valid = valid,
    };
    frame.jittered_projection =
        temporal_jitter_projection(view.projection, frame.sample_offset_pixels, extent);
    frame.jittered_view_projection = frame.jittered_projection * view.view;
    frame.inverse_jittered_view_projection = glm::inverse(frame.jittered_view_projection);
    if (!finite(frame.inverse_jittered_view_projection)) {
        throw std::invalid_argument("temporal view cannot be reconstructed");
    }
    pending_ = Pending{view, frame};
    return frame;
}

void TemporalViewState::commit(std::uint64_t token, vulkan::GpuSubmissionTicket ticket) {
    if (!pending_ || pending_->frame.token != token) {
        throw std::logic_error("temporal commit does not match a pending frame");
    }
    if (ticket.value == 0U || ticket.value <= last_submission_.value) {
        throw std::invalid_argument("temporal commit requires a new accepted submission ticket");
    }
    accepted_ = Accepted{pending_->view, pending_->frame};
    pending_.reset();
    invalidation_ = TemporalResetReason::None;
    last_submission_ = ticket;
    ++submitted_frames_;
}

void TemporalViewState::discard() {
    pending_.reset();
    invalidation_ = TemporalResetReason::DiscardedFrame;
}

void TemporalViewState::invalidate(TemporalResetReason reason) {
    if (pending_) {
        throw std::logic_error("cannot invalidate a pending temporal frame");
    }
    if (reason == TemporalResetReason::None) {
        throw std::invalid_argument("temporal invalidation needs a reset reason");
    }
    invalidation_ = reason;
}

bool TemporalViewState::pending() const noexcept {
    return pending_.has_value();
}

std::uint64_t TemporalViewState::submitted_frames() const noexcept {
    return submitted_frames_;
}

vulkan::GpuSubmissionTicket TemporalViewState::last_submission() const noexcept {
    return last_submission_;
}

} // namespace cubey::render
