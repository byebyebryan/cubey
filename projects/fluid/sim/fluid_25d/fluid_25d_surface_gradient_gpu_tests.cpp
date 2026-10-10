#include <cubey/core/math.h>
#include <cubey/procedural/noise.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using cubey::math::Vec2;
using cubey::math::Vec3;
using cubey::math::Vec4;
constexpr std::array<Vec3, 16> normals{{{0, 1, 0},
                                        {1, 0, 0},
                                        {-1, 0, 0},
                                        {0, 0, 1},
                                        {0, 0, -1},
                                        {0, -1, 0},
                                        {0.3F, 1, 0.6F},
                                        {-0.3F, 1, -0.6F},
                                        {0.70711F, 0.70710F, 0},
                                        {0.70709F, 0.70710F, 0},
                                        {1.00001F, 1, 1},
                                        {0.99999F, 1, 1},
                                        {0.99F, 0.01F, 0},
                                        {-0.99F, 0.01F, 0},
                                        {0.01F, 0.01F, 0.99F},
                                        {0.01F, 0.01F, -0.99F}}};

Vec3 expected(std::size_t i, unsigned mode) {
    const auto n = glm::normalize(normals[i]);
    if (mode == 1U || mode == 2U || mode == 3U)
        return n; // Zero strength, flat maps, or distant fade are identities.
    auto weights = glm::pow(glm::abs(n), Vec3{4.0F});
    weights /= weights.x + weights.y + weights.z;
    // Independent analytic linear-height fields in ZY, XZ, XY planes.
    const Vec3 x = mode == 4U ? Vec3{0, 128, 128} : Vec3{0, -0.1F, 0.3F};
    const Vec3 y = mode == 4U ? Vec3{128, 0, 128} : Vec3{0.2F, 0, 0.3F};
    const Vec3 z = mode == 4U ? Vec3{128, 128, 0} : Vec3{0.2F, -0.1F, 0};
    const auto gradient = weights.x * x + weights.y * y + weights.z * z;
    const auto tangent = gradient - glm::dot(n, gradient) * n;
    return glm::normalize(n - tangent * (mode == 4U ? 2.0F : 0.12F));
}

void run() {
    cubey::vulkan::Instance instance({.application_name = "fluid25d surface gradient controls",
                                      .application_version = 0,
                                      .required_extensions = {},
                                      .validation = true,
                                      .require_validation = false});
    cubey::vulkan::Device device(instance, {.required_queue_flags = VK_QUEUE_COMPUTE_BIT,
                                            .require_present = false,
                                            .require_dynamic_rendering = false});
    cubey::vulkan::SubmissionCoordinator submission(device);
    constexpr auto bytes = sizeof(Vec4) * 1046U;
    cubey::vulkan::Buffer storage(
        device, cubey::vulkan::device_local_buffer_config(
                    bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    cubey::vulkan::Buffer readback(device, cubey::vulkan::readback_buffer_config(bytes));
    std::array<float, 256> beds{}, depths{};
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
            depths[y * 16U + x] = x + y >= 15U ? 0.4F : 0.02F;
    const cubey::vulkan::BufferConfig field_config{.size = sizeof(depths),
                                                   .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                   .memory_properties =
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    cubey::vulkan::Buffer bed_buffer(device, field_config), depth_buffer(device, field_config);
    bed_buffer.upload(beds.data(), sizeof(beds));
    depth_buffer.upload(depths.data(), sizeof(depths));
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 3> bindings{
        {{.binding = 0,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
         {.binding = 1,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
         {.binding = 2,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT}}};
    cubey::vulkan::DescriptorSetBundle descriptors(device,
                                                   cubey::vulkan::DescriptorSetInfo(bindings));
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(descriptors.set(), 0, storage.handle(), storage.size());
    writes.storage_buffer(descriptors.set(), 1, bed_buffer.handle(), bed_buffer.size());
    writes.storage_buffer(descriptors.set(), 2, depth_buffer.handle(), depth_buffer.size());
    writes.update(device);
    const std::array layouts{descriptors.layout()};
    cubey::render::ComputePipelineResource pipeline(
        device, {.shader_stage = cubey::render::compute_shader_file(
                     std::filesystem::path{CUBEY_FLUID25D_NORMAL_TEST_SHADER_DIR} /
                     "fluid_25d_surface_gradient_test.comp.spv"),
                 .descriptor_set_layouts = layouts});
    cubey::vulkan::ImmediateCommands commands(device, submission);
    const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptors.set());
    recorder.dispatch(1, 1, 1);
    const VkBufferMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                        .pNext = nullptr,
                                        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .buffer = storage.handle(),
                                        .offset = 0,
                                        .size = storage.size()};
    recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, {}, {&barrier, 1}, {});
    const VkBufferCopy copy{.srcOffset = 0, .dstOffset = 0, .size = bytes};
    vkCmdCopyBuffer(commands.command_buffer(), storage.handle(), readback.handle(), 1, &copy);
    commands.submit_and_wait();
    std::array<Vec4, 1046> values{};
    readback.download(values.data(), bytes);
    for (unsigned mode = 0; mode < 5; ++mode) {
        for (std::size_t i = 0; i < normals.size(); ++i) {
            const auto actual = values[mode * 16U + i];
            const auto target = expected(i, mode);
            if (!std::isfinite(actual.w) || glm::length(Vec3{actual} - target) > 0.00005F ||
                std::abs(glm::length(Vec3{actual}) - 1.0F) > 0.00001F ||
                std::abs(actual.w) > 0.0001F)
                throw std::runtime_error("GPU surface-gradient analytic control failed at mode " +
                                         std::to_string(mode) + " normal " + std::to_string(i));
        }
    }
    if (glm::length(Vec3{values[8]} - Vec3{values[9]}) > 0.0001F ||
        glm::length(Vec3{values[10]} - Vec3{values[11]}) > 0.0001F)
        throw std::runtime_error("GPU projection-seam continuity failed");
    for (std::size_t i = 80U; i < 86U; ++i) {
        const auto target = i >= 82U && i <= 84U ? glm::normalize(Vec3{-0.4F, 1.0F, -0.6F})
                                                 : Vec3{0.0F, 1.0F, 0.0F};
        if (glm::length(Vec3{values[i]} - target) > 0.00005F)
            throw std::runtime_error("wet-only water surface normal control failed");
    }
    // Independent CPU analytic slopes and removed-band variance, including
    // zero strength, all subpixel, and the shader's decorative clock wrap.
    const std::array<Vec3, 3> bands{
        {{0.8F, 0.6F, 1.0F}, {0.96F, 0.28F, 0.47F}, {0.6F, 0.8F, 0.22F}}};
    const std::array amplitudes{1.0F, 0.5F, 0.25F};
    for (unsigned i = 0; i < 16U; ++i) {
        Vec3 target{0.0F};
        const float strength = i % 8U < 4U ? 0.12F : 0.0F;
        const float footprint = static_cast<float>(i % 4U) * 16.0F;
        for (std::size_t band = 0; band < bands.size(); ++band) {
            const float wavelength = 48.0F * bands[band].z;
            const float t =
                std::clamp((footprint - wavelength * 0.125F) / (wavelength * 0.375F), 0.0F, 1.0F);
            const float resolved = 1.0F - t * t * (3.0F - 2.0F * t);
            const float amplitude = strength * amplitudes[band];
            target.x += bands[band].x * amplitude * resolved;
            target.y += bands[band].y * amplitude * resolved;
            target.z += 0.5F * amplitude * amplitude * (1.0F - resolved * resolved);
        }
        if (glm::length(Vec3{values[86U + i]} - target) > 0.00005F)
            throw std::runtime_error("ripple identity/filter/variance/clock-wrap control failed");
    }
    const auto axis = glm::normalize(cubey::math::Vec2{0.3F, 0.7F});
    const cubey::math::Vec2 side{-axis.y, axis.x};
    for (unsigned i = 0; i < 64U; ++i) {
        Vec3 target{0.0F};
        const double wavelength = i < 32U ? 4.0 : 1.25;
        const double variance = i % 16U < 8U ? 0.07 : 0.0;
        const double footprint = double(i % 8U) * wavelength * 0.1;
        const double amplitude = std::sqrt(2.0 * variance / 1.3125);
        for (std::size_t band = 0; band < bands.size(); ++band) {
            const double lambda = wavelength * double(bands[band].z);
            const double t = std::clamp((footprint - lambda * 0.125) / (lambda * 0.375), 0.0, 1.0);
            const double resolved = 1.0 - t * t * (3.0 - 2.0 * t);
            const double a = amplitude * double(amplitudes[band]);
            const double phase =
                6.28318530718 *
                ((17.3 * double(bands[band].x) - 29.2 * double(bands[band].y)) / lambda +
                 double(band) * 0.37);
            const auto direction = axis * bands[band].x + side * bands[band].y;
            target.x += direction.x * float(a * resolved * std::sin(phase));
            target.y += direction.y * float(a * resolved * std::sin(phase));
            target.z += float(0.5 * a * a * (1.0 - resolved * resolved));
        }
        const auto actual = Vec3{values[102U + i]};
        if (!std::isfinite(actual.x) || !std::isfinite(actual.y) || !std::isfinite(actual.z) ||
            glm::length(actual - target) > 0.0005F || actual.z < 0.0F ||
            actual.z > float(variance) + 1e-6F)
            throw std::runtime_error("agitation slope/filter/variance/wrap control failed: " +
                                     std::to_string(i));
    }
    const std::array fade_depths{0.0F, 0.006F, 0.02F, 0.05F, 0.15F, 0.299F, 0.30F, 2.0F};
    for (unsigned i = 0; i < 24U; ++i) {
        const float strength = float(i / 8U) * 0.5F;
        const float t = std::clamp(fade_depths[i % 8U] / 0.30F, 0.0F, 1.0F);
        const float expected_fade = 1.0F - strength + strength * t * t * (3.0F - 2.0F * t);
        const float actual = values[166U + i].x;
        if (!std::isfinite(actual) || actual < 0.0F || actual > 1.0F ||
            std::abs(actual - expected_fade) > 0.00001F ||
            (i % 8U > 0U && actual < values[165U + i].x))
            throw std::runtime_error("shallow coverage identity/bounds/monotonicity failed");
    }
    const auto smooth = [](float low, float high, float value) {
        const float t = std::clamp((value - low) / (high - low), 0.0F, 1.0F);
        return t * t * (3.0F - 2.0F * t);
    };
    const std::array<float, 4> rapid_depths{0.0F, 0.02F, 0.08F, 2.0F};
    for (unsigned i = 0; i < 96U; ++i) {
        // Flat, stationary, downhill, cross-slope, uphill, gentle, extreme,
        // near-stationary. Height exaggeration must not change classification.
        const unsigned mode = i % 8U;
        const float grade = mode == 2U || mode == 6U ? 1.0F : 0.0F;
        const float speed = mode == 1U ? 0.0F : mode == 7U ? 0.04F : 3.0F;
        const float target =
            smooth(0.08F, 1.4F, speed) * grade * smooth(0.002F, 0.08F, rapid_depths[(i / 8U) % 4U]);
        const float actual = values[190U + i].x;
        if (!std::isfinite(actual) || actual < 0.0F || actual > 1.0F ||
            std::abs(actual - target) > 0.00001F)
            throw std::runtime_error("rapid directional/depth/height control failed: " +
                                     std::to_string(i));
    }
    const std::array clocks{0.0F, 0.001F, 7.999F, 8.0F, 8.001F, 1023.999F, 1024.0F, 1024.001F};
    for (unsigned i = 0; i < 16U; ++i) {
        const float time = clocks[i % 8U] / 8.0F + (i < 8U ? 0.0F : 0.13F);
        const float a = time - std::floor(time);
        const float b = time + 0.5F - std::floor(time + 0.5F);
        const Vec4 target{a, b, 1.0F - std::abs(a * 2 - 1), 1.0F - std::abs(b * 2 - 1)};
        const auto actual = values[286U + i];
        if (glm::length(actual - target) > 0.00005F ||
            std::abs(actual.z + actual.w - 1.0F) > 0.00001F)
            throw std::runtime_error("rapid phase/weight/reset control failed");
    }
    if (glm::length(values[286] - values[292]) > 0.00001F ||
        glm::length(values[294] - values[300]) > 0.00005F)
        throw std::runtime_error("rapid 1024-second clock wrap failed");
    const std::array<Vec2, 8> grades{Vec2{0},    Vec2{1, 0},    Vec2{1, 0},    Vec2{1, 0},
                                     Vec2{1, 0}, Vec2{0.1F, 0}, Vec2{1000, 0}, Vec2{1, 0}};
    const std::array<Vec2, 8> velocities{Vec2{3, 0},       Vec2{0},        Vec2{-3, 0},
                                         Vec2{0, 3},       Vec2{3, 0},     Vec2{-3, 0},
                                         Vec2{-100000, 0}, Vec2{-0.04F, 0}};
    const std::array heights{0.25F, 1.0F, 4.0F};
    for (unsigned i = 0; i < 24; ++i) {
        const auto u = velocities[i % 8U];
        const auto g = grades[i % 8U];
        const float height = heights[i / 8U];
        const Vec3 n = glm::normalize(Vec3{-g.x * height, 1, -g.y * height});
        Vec3 expected{0};
        if (glm::length(u) > 0.00001F) {
            const Vec3 tangent{u.x, -glm::dot(Vec2{n.x, n.z}, u) / (std::max(n.y, 0.05F) * height),
                               u.y};
            expected =
                glm::normalize(tangent) * std::clamp(glm::length(tangent) * 3.0F, 6.0F, 24.0F);
        }
        const Vec3 actual{values[302U + i]};
        if (!std::isfinite(actual.x) || !std::isfinite(actual.y) || !std::isfinite(actual.z) ||
            glm::length(actual - expected) > 0.0001F || glm::length(actual) > 24.0001F)
            throw std::runtime_error("cascade lifted/bounded flow control failed");
    }
    const std::array local_grades{0.0F, 1.0F, 0.3F, -0.1F, 0.0F, 0.0F, 0.0F, 0.0F};
    const std::array upstream_grades{1.0F, 1.0F, 0.3F, 1.0F, 0.1F, 1.0F, 1.0F, 1.0F};
    const std::array landing_depths{0.0F, 0.08F, 0.14F, 0.30F};
    for (unsigned i = 0; i < 64; ++i) {
        const unsigned mode = i % 8U;
        const float local = local_grades[mode], up = upstream_grades[mode];
        const float depth = mode == 5U || mode == 6U ? 0.0F : landing_depths[(i / 8U) % 4U];
        const float speed = mode == 7U ? 0.0F : i < 32U ? 3.0F : 0.08F;
        const float expected =
            smooth(0.08F, 0.20F, depth) * smooth(0.08F, 1.4F, speed) * smooth(0.4F, 1.0F, up) *
            (1.0F - smooth(0.25F, 0.75F, std::max(local, 0.0F))) *
            smooth(0.15F, 0.7F, up - std::max(local, 0.0F)) * (local >= -0.05F ? 1.0F : 0.0F);
        const float actual = values[326U + i].x;
        if (!std::isfinite(actual) || actual < 0 || actual > 1 ||
            std::abs(actual - expected) > 0.00001F)
            throw std::runtime_error("landing support/direction/flattening control failed");
    }
    for (unsigned i = 0; i < 16U; ++i) {
        const float target = 0.94F * (float(i % 4U) / 3.0F) * (float(i / 4U) / 3.0F);
        const float actual = values[390U + i].x;
        if (!std::isfinite(actual) || actual < 0.0F || actual > 0.94F ||
            std::abs(actual - target) > 0.00001F)
            throw std::runtime_error("cascade zero-floor/peak control failed");
    }
    const float phase = cubey::procedural::hash_to_unit_masked_24(0x25d2026U + 2U);
    for (unsigned i = 0; i < 64U; ++i) {
        const float clock = float(i % 8U) * 0.5F;
        const float age = clock * 0.5F + phase - std::floor(clock * 0.5F + phase);
        const unsigned mode = (i / 8U) % 4U;
        const Vec2 velocity = mode == 2U ? Vec2{30, -40} : mode == 3U ? Vec2{0} : Vec2{3, -4};
        const Vec2 offset = velocity / std::max(1.0F, glm::length(velocity) / 12.0F) *
                            (age * 2.0F * (mode == 1U ? 2.0F : 1.0F)) / 10.0F;
        const Vec4 target{offset, age, smooth(0, 0.12F, age) * (1.0F - smooth(0.65F, 1, age))};
        if (glm::length(values[406U + i] - target) > 0.00006F) {
            std::cerr << "motion case " << i << " actual " << values[406U + i].x << ','
                      << values[406U + i].y << ',' << values[406U + i].z << ','
                      << values[406U + i].w << " expected " << target.x << ',' << target.y << ','
                      << target.z << ',' << target.w << '\n';
            throw std::runtime_error(
                "whitewater direction/cap/speed/life/fps/wrap GPU control failed");
        }
    }
    const std::array stream_depths{0.0F, 0.04F, 0.14F, 0.4F};
    const std::array stream_speeds{0.0F, 0.7F, 2.0F, 4.0F};
    for (unsigned i = 0; i < 32U; ++i) {
        const float expected_stream = i < 16U
                                          ? smooth(0.8F, 4.0F, stream_speeds[i % 4U]) *
                                                smooth(0.08F, 0.20F, stream_depths[(i / 4U) % 4U])
                                          : 0.0F;
        if (std::abs(values[470U + i].x - expected_stream) > 0.00002F)
            throw std::runtime_error(
                "general-stream speed/depth/steep-preservation GPU control failed");
    }
    const std::array current_flow{Vec2{0}, Vec2{3, -4}, Vec2{-3, 4}, Vec2{1, 0}};
    const std::array filtered_flow{Vec2{3, -4}, Vec2{-3, 4}, Vec2{0}, Vec2{0, 1}};
    for (unsigned i = 0; i < 16U; ++i) {
        const auto current = current_flow[i % 4U], filtered = filtered_flow[i / 4U];
        const auto expected_flow =
            glm::length(current) <= 0.00001F || glm::dot(current, filtered) <= 0 ? current
                                                                                 : filtered;
        if (glm::length(values[502U + i] - Vec4{expected_flow, 0, 0}) > 0.00001F)
            throw std::runtime_error("whitewater stop/reversal/stale-direction GPU control failed");
    }
    const std::array foam_samples{Vec2{0.8F, 0.2F}, Vec2{0.72F, 0.67F}, Vec2{0.65F},
                                  Vec2{0.1F, 0.3F}};
    const std::array patchiness{0.0F, 0.1F, 2.0F / 3.0F, 1.0F};
    for (unsigned i = 0; i < 80U; ++i) {
        const auto samples = foam_samples[(i / 5U) % 4U];
        const float weight = float(i % 5U) / 4.0F;
        const float patches = patchiness[i / 20U];
        const float dense = smooth(0.495F, 0.625F, samples.x * weight + samples.y * (1 - weight));
        const float low = 0.50F + (0.65F - 0.50F) * patches;
        const float high = 0.62F + (0.75F - 0.62F) * patches;
        const float shaped = smooth(low - 0.005F, high + 0.005F, samples.x) * weight +
                             smooth(low - 0.005F, high + 0.005F, samples.y) * (1 - weight);
        const float target = dense + (shaped - dense) * smooth(0, 0.2F, patches);
        if (!std::isfinite(values[518U + i].x) || std::abs(values[518U + i].x - target) > 0.00002F)
            throw std::runtime_error("stream foam patchiness/phase/legacy GPU control failed");
    }
    if (std::abs(values[578U + 2U].x - 0.5F) > 0.00002F || values[518U + 2U].x >= 0.005F)
        throw std::runtime_error(
            "sparse midpoint must crossfade patches, not threshold their mean");
    for (unsigned i = 0; i < 16U; ++i) {
        const auto domain = values[598U + i];
        if (!std::isfinite(domain.x) || !std::isfinite(domain.y) || !std::isfinite(domain.z) ||
            !std::isfinite(domain.w) || domain.x < 0 || domain.x > 1 || domain.y < 0 ||
            domain.y > 1 || domain.z <= 0.0001F || domain.w <= 0.0001F)
            throw std::runtime_error("stream foam domain must break 64 m repetition on both axes");
        const float weight = float(i % 5U) / 4.0F;
        const float expected = float(i % 4U) / 3.0F * weight + float(i / 4U) / 3.0F * (1 - weight);
        if (!std::isfinite(values[614U + i].x) ||
            std::abs(values[614U + i].x - expected) > 0.00002F)
            throw std::runtime_error("stream foam envelope must gate each layer before crossfade");
    }
    for (unsigned i = 0; i < 16U; ++i) {
        const auto expected_gradient = i == 15U ? Vec2{0} : Vec2{0.2F, -0.3F};
        if (glm::length(Vec2{values[630U + i]} - expected_gradient) > 0.00002F)
            throw std::runtime_error(
                "bank gradient must be projection independent and guard degeneracy");
        const float peak = float(i % 4U) * 0.08F;
        const float expected_support =
            (i < 8U || i >= 12U) ? smooth(0.12F, 0.20F, peak + 0.03F) : 0.0F;
        if (std::abs(values[646U + i].x - expected_support) > 0.00002F)
            throw std::runtime_error("bank support must exclude all-wet, dry and thin sheets");
        const auto phases = values[726U + i];
        if (glm::any(glm::isnan(phases)) || glm::any(glm::isinf(phases)) ||
            glm::any(glm::lessThan(phases, Vec4{0})) ||
            glm::any(glm::greaterThan(phases, Vec4{1})) ||
            std::abs(phases.x - phases.y) > 0.00003F || std::abs(phases.z - phases.w) > 0.002F)
            throw std::runtime_error("bank motion wrap/reset must be bounded and continuous");
        const auto calm = values[742U + i];
        if (glm::length(Vec2{calm} - Vec2{calm.z, calm.w}) > 0.000001F)
            throw std::runtime_error("stationary bank pattern cannot crawl with clock advancement");
    }
    bool moving_edge_changed = false;
    for (unsigned i = 0; i < 64U; ++i) {
        const auto edge = values[662U + i];
        if (!std::isfinite(edge.x) || !std::isfinite(edge.y) || edge.x < 0 || edge.x > 4.5F ||
            edge.y < 0 || edge.y > 1 || (i % 8U != 7U && edge.x != 0))
            throw std::runtime_error(
                "bank inset must be capped, localized and inactive for controls");
        if (i % 8U == 7U && i > 7U)
            moving_edge_changed |= std::abs(edge.x - values[669U].x) > 0.00001F;
    }
    if (!moving_edge_changed)
        throw std::runtime_error("moving shoreline detail must actually move");
    bool bold_edge_changed = false, bold_edge_pronounced = false;
    for (unsigned i = 0; i < 32U; ++i) {
        const auto edge = values[758U + i];
        if (glm::any(glm::isnan(edge)) || glm::any(glm::isinf(edge)) || edge.x < 0 ||
            edge.x > 13.50001F || edge.z < 0 || edge.z > 13.50001F || edge.y < 0 || edge.y > 1 ||
            edge.w < 0 || edge.w > 1 || (i < 16U && glm::length(edge) > 0.0F))
            throw std::runtime_error(
                "pronounced banks remain bounded and exclude uniform/depth-film controls: " +
                std::to_string(i) + " inset=" + std::to_string(edge.x) + "/" +
                std::to_string(edge.z));
        if (i >= 16U) {
            bold_edge_changed |= std::abs(edge.x - edge.z) > 0.1F;
            bold_edge_pronounced |= edge.x > 4.5F;
        }
    }
    if (!bold_edge_changed || !bold_edge_pronounced)
        throw std::runtime_error("pronounced bank probe must exceed the old cap and visibly vary");
    const auto basis = [](double t) {
        const double t2 = t * t, t3 = t2 * t;
        return std::array{std::pow(1.0 - t, 3) / 6.0, (3.0 * t3 - 6.0 * t2 + 4.0) / 6.0,
                          (-3.0 * t3 + 3.0 * t2 + 3.0 * t + 1.0) / 6.0, t3 / 6.0};
    };
    const auto derivative = [](double t) {
        return std::array{-0.5 * (1.0 - t) * (1.0 - t), 1.5 * t * t - 2.0 * t,
                          -1.5 * t * t + t + 0.5, 0.5 * t * t};
    };
    bool active_seam = false;
    for (unsigned i = 0; i < 128U; ++i) {
        const unsigned pair = (i % 64U) / 2U;
        Vec2 p{6.0F + float(pair % 4U), 9.0F - float(pair % 4U) + 0.25F};
        if (pair >= 8U && pair < 16U)
            p = {p.y, p.x};
        if (pair >= 16U && pair < 24U)
            p = {7.5F, 7.75F};
        if (pair >= 24U)
            p = {7.75F, 7.75F};
        (pair >= 8U && pair < 16U ? p.y : p.x) +=
            (i % 2U == 0U ? -1.0F : 1.0F) * (i < 64U ? 0.0001F : 0.00001F);
        const auto wx = basis(p.x - std::floor(p.x)), wy = basis(p.y - std::floor(p.y));
        const auto dx = derivative(p.x - std::floor(p.x)), dy = derivative(p.y - std::floor(p.y));
        Vec3 target{0};
        for (std::size_t y = 0; y < 4; ++y)
            for (std::size_t x = 0; x < 4; ++x) {
                const int cx = std::clamp(int(std::floor(p.x)) - 1 + int(x), 0, 15);
                const int cy = std::clamp(int(std::floor(p.y)) - 1 + int(y), 0, 15);
                const double value = depths[std::size_t(cy * 16 + cx)];
                target += Vec3{float(value * wx[x] * wy[y]), float(value * dx[x] * wy[y] / 30.0),
                               float(value * wx[x] * dy[y] / 30.0)};
            }
        const auto actual = values[790U + i];
        if (glm::any(glm::isnan(actual)) || glm::any(glm::isinf(actual)) ||
            glm::length(Vec3{actual} - target) > 0.000002F || actual.w < 0 || actual.w > 13.50001F)
            throw std::runtime_error("B-spline bank depth/analytic gradient/cap control failed");
        if (i % 2U == 1U) {
            const float delta = glm::length(actual - values[789U + i]);
            if (delta > (i < 64U ? 0.03F : 0.003F))
                throw std::runtime_error(
                    "bank inset must be continuous across native/refined/diagonal seams: pair=" +
                    std::to_string(pair) + " inset=" + std::to_string(values[789U + i].w) + "/" +
                    std::to_string(actual.w) +
                    " delta=" + std::to_string(glm::length(actual - values[789U + i])));
            if (i >= 64U &&
                delta >
                    0.2F * glm::length(values[790U + i - 64U] - values[789U + i - 64U]) + 0.0001F)
                throw std::runtime_error(
                    "bank seam delta must shrink with sample separation, not remain a jump");
            active_seam |= actual.w > 1.0F;
        }
    }
    if (!active_seam)
        throw std::runtime_error("seam controls must exercise a positive strong inset");
    const auto wet_depth = [&](Vec2 position, bool cubic) {
        const auto p = glm::clamp(position, Vec2{0}, Vec2{15});
        const auto cell = [&](int x, int y) {
            return double(depths[std::size_t(std::clamp(y, 0, 15) * 16 + std::clamp(x, 0, 15))]);
        };
        const int x = int(std::floor(p.x)), y = int(std::floor(p.y));
        const double fx = double(p.x) - x, fy = double(p.y) - y;
        if (!cubic)
            return float(
                fx >= fy
                    ? cell(x, y) * (1 - fx) + cell(x + 1, y) * (fx - fy) + cell(x + 1, y + 1) * fy
                    : cell(x, y) * (1 - fy) + cell(x, y + 1) * (fy - fx) + cell(x + 1, y + 1) * fx);
        const auto wx = basis(fx), wy = basis(fy);
        double h = 0;
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                h += cell(x - 1 + column, y - 1 + row) * wx[std::size_t(column)] *
                     wy[std::size_t(row)];
        return float(h);
    };
    const auto wet_result = [&](float h) {
        const float wet = smooth(0.002F, 0.012F, h) * (1 - smooth(0.02F, 0.05F, h));
        return Vec4{h, wet, 1 + (0.86F - 1) * wet, 0.94F + (0.78F - 0.94F) * wet};
    };
    bool different_sampling = false, active_wet_seam = false;
    for (unsigned i = 0; i < 64U; ++i) {
        const unsigned pair = i % 32U;
        const float offset = float(pair / 4U) / 8.0F;
        Vec2 p{6.0F + float(pair % 4U) + offset, 7.05F - float(pair % 4U) - offset};
        if (pair >= 28U)
            p = {pair % 2U == 0U ? -0.5F : 15.5F, pair < 30U ? -0.5F : 15.5F};
        const auto actual = values[918U + i];
        const auto target = wet_result(wet_depth(p, i >= 32U));
        if (glm::any(glm::isnan(actual)) || glm::any(glm::isinf(actual)) ||
            glm::length(actual - target) > 0.00002F || actual.y < 0 || actual.y > 1)
            throw std::runtime_error("terrain wetness sampling/weight/bounds control failed: " +
                                     std::to_string(i));
        if (i >= 32U)
            different_sampling |= std::abs(actual.y - values[886U + i].y) > 0.05F;
    }
    for (unsigned i = 0; i < 48U; ++i) {
        const unsigned pair = (i % 24U) / 2U;
        Vec2 p{6.0F + float(pair % 4U), 7.05F - float(pair % 4U)};
        if (pair >= 4U && pair < 8U)
            p = {p.y, p.x};
        if (pair >= 8U)
            p = pair < 10U ? Vec2{6.5F, 6.55F} : Vec2{6.525F};
        (pair >= 4U && pair < 8U ? p.y : p.x) +=
            (i % 2U == 0U ? -1.0F : 1.0F) * (i < 24U ? 0.001F : 0.0001F);
        const auto actual = values[982U + i];
        if (glm::any(glm::isnan(actual)) || glm::any(glm::isinf(actual)) ||
            glm::length(actual - wet_result(wet_depth(p, true))) > 0.00002F)
            throw std::runtime_error("terrain wetness seam CPU/GPU parity failed");
        active_wet_seam |= actual.y > 0.05F && actual.y < 0.95F;
        if (i % 2U == 1U) {
            const float delta = glm::length(actual - values[981U + i]);
            if (delta > (i < 24U ? 0.015F : 0.0015F) ||
                (i >= 24U &&
                 delta > 0.2F * glm::length(values[958U + i] - values[957U + i]) + 0.00002F))
                throw std::runtime_error("terrain wetness retains a grid/mesh-diagonal seam jump");
        }
    }
    const std::array wet_depths{-0.01F, 0.0F,  0.001F, 0.002F, 0.003F, 0.006F, 0.012F, 0.02F,
                                0.03F,  0.04F, 0.049F, 0.05F,  0.06F,  0.2F,   1.0F,   52.0F};
    for (unsigned i = 0; i < wet_depths.size(); ++i)
        if (!std::isfinite(values[1030U + i].x) ||
            std::abs(values[1030U + i].x - wet_result(wet_depths[i]).y) > 0.00001F)
            throw std::runtime_error("terrain current-film thresholds changed");
    if (!different_sampling || !active_wet_seam)
        throw std::runtime_error("terrain wetness controls must exercise the reconstruction fix");
    std::cout << "PASS: 918 retained + 128 terrain-wetness GPU controls (selection, thresholds, "
                 "clamped edges and continuous colour/roughness seams)\n";
}
} // namespace

int main() {
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        const std::string_view text{error.what()};
        if (text.find("no Vulkan physical devices found") != std::string_view::npos ||
            text.find("vkEnumeratePhysicalDevices") != std::string_view::npos)
            return 77;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
