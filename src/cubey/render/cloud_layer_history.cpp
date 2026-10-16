#include <cubey/render/cloud_layer.h>

#include <cubey/render/generated_texture.h>
#include <cubey/render/pass.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cubey::render {

namespace {

[[nodiscard]] Texture2DConfig cloud_layer_history_texture_config(VkExtent2D extent) {
    return {
        .extent = extent,
        .format = kCloudLayerColorFormat,
        .usage = Texture2DUsage::StorageSampled,
        .create_sampler = false,
        .sampler = {},
    };
}

[[nodiscard]] bool cloud_layer_near(float lhs, float rhs, float tolerance = 0.001F) {
    return std::abs(lhs - rhs) <= tolerance;
}

[[nodiscard]] bool cloud_layer_near(math::Vec4 lhs, math::Vec4 rhs,
                                    float tolerance = 0.001F) {
    return cloud_layer_near(lhs.x, rhs.x, tolerance) &&
           cloud_layer_near(lhs.y, rhs.y, tolerance) &&
           cloud_layer_near(lhs.z, rhs.z, tolerance) &&
           cloud_layer_near(lhs.w, rhs.w, tolerance);
}

[[nodiscard]] bool cloud_layer_history_uniforms_compatible(
    const CloudLayerFrameUniforms& previous, const CloudLayerFrameUniforms& current) {
    const math::Vec4 previous_weather_static{
        previous.weather.x,
        previous.weather.y,
        previous.weather.z,
        0.0F,
    };
    const math::Vec4 current_weather_static{
        current.weather.x,
        current.weather.y,
        current.weather.z,
        0.0F,
    };
    const math::Vec4 previous_temporal_static{
        0.0F,
        0.0F,
        0.0F,
        previous.temporal_options.w,
    };
    const math::Vec4 current_temporal_static{
        0.0F,
        0.0F,
        0.0F,
        current.temporal_options.w,
    };
    return cloud_layer_near(previous.cloud_shell, current.cloud_shell) &&
           cloud_layer_near(previous_weather_static, current_weather_static) &&
           cloud_layer_near(previous.sun_direction_intensity, current.sun_direction_intensity) &&
           cloud_layer_near(previous.sun_color, current.sun_color) &&
           cloud_layer_near(previous.moon_direction_intensity,
                            current.moon_direction_intensity) &&
           cloud_layer_near(previous.moon_color, current.moon_color) &&
           cloud_layer_near(previous.ambient_color_intensity,
                            current.ambient_color_intensity) &&
           cloud_layer_near(previous.ref_options, current.ref_options) &&
           cloud_layer_near(previous.shape_options, current.shape_options) &&
           cloud_layer_near(previous.weather_feature_weights, current.weather_feature_weights) &&
           cloud_layer_near(previous.cloud_color_top_shadow, current.cloud_color_top_shadow) &&
           cloud_layer_near(previous.cloud_color_bottom_horizon,
                            current.cloud_color_bottom_horizon) &&
           cloud_layer_near(previous.lighting_strengths, current.lighting_strengths) &&
           cloud_layer_near(previous.twilight_options, current.twilight_options) &&
           cloud_layer_near(previous.composite_options, current.composite_options) &&
           cloud_layer_near(previous.sampling_options, current.sampling_options) &&
           cloud_layer_near(previous_temporal_static, current_temporal_static) &&
           cloud_layer_near(previous.background_options, current.background_options) &&
           cloud_layer_near(previous.distance_options, current.distance_options) &&
           cloud_layer_near(previous.orbit_options, current.orbit_options) &&
           cloud_layer_near(previous.orbit_shell_options, current.orbit_shell_options) &&
           cloud_layer_near(previous.scene_depth_options, current.scene_depth_options) &&
           cloud_layer_near(previous.density_options, current.density_options) &&
           cloud_layer_near(previous.edge_options, current.edge_options);
}

[[nodiscard]] bool cloud_layer_extent_equal(VkExtent2D lhs, VkExtent2D rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
}


} // namespace

void CloudLayerRuntime::complete_frame(FrameSlot frame_slot, const CloudLayerRuntimeFrame& frame) {
    if (frame.temporal_pass_enabled && frame_slot.index < history_texture_valid_.size() &&
        frame_slot.index < history_read_indices_.size()) {
        history_texture_valid_[frame_slot.index][frame.history_write_index] = true;
        history_read_indices_[frame_slot.index] = frame.history_write_index;
        if (frame_slot.index < history_frame_states_.size()) {
            history_frame_states_[frame_slot.index] = frame.frame_uniforms;
        }
    } else if (!frame.temporal_pass_enabled && frame_slot.index < history_texture_valid_.size()) {
        history_texture_valid_[frame_slot.index] = {false, false};
        if (frame_slot.index < history_frame_states_.size()) {
            history_frame_states_[frame_slot.index].reset();
        }
    }
    ++temporal_frame_index_;
}

const Texture2D& CloudLayerRuntime::history_cloud_texture(FrameSlot frame_slot,
                                                         std::uint32_t ping_pong) const {
    if (frame_slot.index >= history_cloud_textures_.size() || ping_pong >= 2U ||
        !history_cloud_textures_[frame_slot.index][ping_pong].has_value()) {
        throw std::runtime_error("cloud layer history texture is not initialized");
    }
    return history_cloud_textures_[frame_slot.index][ping_pong].value();
}

const Texture2D& CloudLayerRuntime::history_metadata_texture(FrameSlot frame_slot,
                                                            std::uint32_t ping_pong) const {
    if (frame_slot.index >= history_metadata_textures_.size() || ping_pong >= 2U ||
        !history_metadata_textures_[frame_slot.index][ping_pong].has_value()) {
        throw std::runtime_error("cloud layer metadata history texture is not initialized");
    }
    return history_metadata_textures_[frame_slot.index][ping_pong].value();
}

bool CloudLayerRuntime::history_texture_valid(FrameSlot frame_slot,
                                              std::uint32_t ping_pong) const {
    return frame_slot.index < history_texture_valid_.size() && ping_pong < 2U &&
           history_texture_valid_[frame_slot.index][ping_pong];
}

bool CloudLayerRuntime::history_extent_matches(VkExtent2D extent) const noexcept {
    if (history_cloud_textures_.empty() || history_cloud_textures_.front().front() == std::nullopt) {
        return false;
    }
    return cloud_layer_extent_equal(history_cloud_textures_.front().front()->extent(), extent);
}

CloudLayerTemporalUniforms CloudLayerRuntime::temporal_frame_uniforms(
    FrameSlot frame_slot, CloudLayerFrameUniforms current,
    std::uint32_t history_read_index) const {
    const bool has_frame_state = frame_slot.index < history_frame_states_.size() &&
                                 history_frame_states_[frame_slot.index].has_value();
    const bool reset =
        !history_texture_valid(frame_slot, history_read_index) || !has_frame_state ||
        !cloud_layer_history_uniforms_compatible(
            has_frame_state ? history_frame_states_[frame_slot.index].value() : current, current);
    const CloudLayerFrameUniforms previous =
        reset ? current : history_frame_states_[frame_slot.index].value();
    const float current_weight = reset ? 1.0F : current.temporal_options.z;
    return {
        .current_camera_right_aspect = current.camera_right_aspect,
        .current_camera_up_tan_half_fovy = current.camera_up_tan_half_fovy,
        .current_camera_forward_mode = current.camera_forward_mode,
        .current_camera_position_radius = current.camera_position_radius,
        .previous_camera_right_aspect = previous.camera_right_aspect,
        .previous_camera_up_tan_half_fovy = previous.camera_up_tan_half_fovy,
        .previous_camera_forward_mode = previous.camera_forward_mode,
        .previous_camera_position_radius = previous.camera_position_radius,
        .current_weather = current.weather,
        .previous_weather = previous.weather,
        .options =
            {
                current_weight,
                reset ? 1.0F : 0.0F,
                current.temporal_options.y,
                current.temporal_options.w,
            },
    };
}

void CloudLayerRuntime::create_history_textures(const cubey::vulkan::Device& device,
                                                VkExtent2D extent,
                                                std::uint32_t frame_slot_count) {
    history_cloud_textures_.clear();
    history_metadata_textures_.clear();
    history_read_indices_.assign(frame_slot_count, 0U);
    history_texture_valid_.assign(frame_slot_count, {false, false});
    history_frame_states_.assign(frame_slot_count, std::nullopt);
    history_cloud_textures_.resize(frame_slot_count);
    history_metadata_textures_.resize(frame_slot_count);
    for (std::array<std::optional<Texture2D>, 2>& textures : history_cloud_textures_) {
        for (std::optional<Texture2D>& texture : textures) {
            texture.emplace(device, cloud_layer_history_texture_config(extent));
        }
    }
    for (std::array<std::optional<Texture2D>, 2>& textures : history_metadata_textures_) {
        for (std::optional<Texture2D>& texture : textures) {
            texture.emplace(device, cloud_layer_history_texture_config(extent));
        }
    }
}

void CloudLayerRuntime::invalidate_history() {
    for (std::array<bool, 2>& valid : history_texture_valid_) {
        valid = {false, false};
    }
    std::fill(history_read_indices_.begin(), history_read_indices_.end(), 0U);
    for (std::optional<CloudLayerFrameUniforms>& frame_state : history_frame_states_) {
        frame_state.reset();
    }
}


} // namespace cubey::render
