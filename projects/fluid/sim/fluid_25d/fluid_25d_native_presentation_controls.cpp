#include "fluid_25d_native_presentation_controls.h"

#include "fluid_25d_commands.h"
#include "fluid_25d_gpu_resources.h"
#include "fluid_25d_presentation.h"
#include "fluid_25d_scenarios.h"

#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/memory_barriers.h>

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace {

constexpr std::uint32_t kGridWidth = 16U;
constexpr std::uint32_t kGridHeight = 8U;
constexpr std::size_t kCellCount = static_cast<std::size_t>(kGridWidth) * kGridHeight;
constexpr float kCellSizeM = 30.0F;
constexpr float kCueTolerance = 2.0e-6F;

[[noreturn]] void fail(const char* control, const char* detail) {
    throw std::runtime_error(std::string("fluid 2.5D native cue GPU control failed: ") + control +
                             ": " + detail);
}

void require(bool condition, const char* control, const char* detail) {
    if (!condition)
        fail(control, detail);
}

template <typename Value> std::vector<std::uint8_t> bytes_of(const std::vector<Value>& values) {
    std::vector<std::uint8_t> bytes(values.size() * sizeof(Value));
    if (!bytes.empty())
        std::memcpy(bytes.data(), values.data(), bytes.size());
    return bytes;
}

std::vector<std::uint8_t> read_buffer(cubey::ProjectGpuServices& gpu,
                                      const cubey::vulkan::Buffer& buffer, const char* label) {
    auto bytes = gpu.readback_buffer(buffer.handle(), buffer.size(), label);
    if (bytes.size() != static_cast<std::size_t>(buffer.size()))
        fail(label, "GPU readback has an unexpected byte size");
    return bytes;
}

std::vector<float> decode_floats(const std::vector<std::uint8_t>& bytes, std::size_t count,
                                 const char* label) {
    if (bytes.size() != count * sizeof(float))
        fail(label, "GPU float buffer has an unexpected byte size");
    std::vector<float> values(count);
    std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

struct HydraulicSnapshot {
    std::vector<std::uint8_t> terrain;
    std::vector<std::uint8_t> depth_a;
    std::vector<std::uint8_t> depth_b;
    std::vector<std::uint8_t> velocity;

    friend bool operator==(const HydraulicSnapshot&, const HydraulicSnapshot&) = default;
};

HydraulicSnapshot read_hydraulic_snapshot(cubey::ProjectGpuServices& gpu,
                                          const Fluid25DGpuResources& resources) {
    return {
        .terrain = read_buffer(gpu, resources.terrain(), "native cue terrain snapshot"),
        .depth_a = read_buffer(gpu, resources.depth_a(), "native cue depth A snapshot"),
        .depth_b = read_buffer(gpu, resources.depth_b(), "native cue depth B snapshot"),
        .velocity = read_buffer(gpu, resources.velocity(), "native cue velocity snapshot"),
    };
}

void require_hydraulic_bytes(const HydraulicSnapshot& actual,
                             const std::vector<std::uint8_t>& terrain,
                             const std::vector<std::uint8_t>& depth,
                             const std::vector<std::uint8_t>& velocity, const char* control) {
    require(actual.terrain == terrain, control, "cue dispatch changed the scratch bed bytes");
    require(actual.depth_a == depth && actual.depth_b == depth, control,
            "cue dispatch changed the uploaded scratch depth bytes");
    require(actual.velocity == velocity, control,
            "cue dispatch changed the uploaded scratch velocity bytes");
}

void require_cue_equal(const std::vector<std::uint8_t>& actual,
                       const std::vector<std::uint8_t>& expected, const char* control,
                       const char* detail) {
    require(actual == expected, control, detail);
}

float bilinear(const std::vector<float>& values, float x, float y) {
    const float bounded_x = std::clamp(x, 0.0F, static_cast<float>(kGridWidth - 1U));
    const float bounded_y = std::clamp(y, 0.0F, static_cast<float>(kGridHeight - 1U));
    const auto lower_x = static_cast<std::uint32_t>(std::floor(bounded_x));
    const auto lower_y = static_cast<std::uint32_t>(std::floor(bounded_y));
    const auto upper_x = std::min(lower_x + 1U, kGridWidth - 1U);
    const auto upper_y = std::min(lower_y + 1U, kGridHeight - 1U);
    const float fraction_x = bounded_x - static_cast<float>(lower_x);
    const float fraction_y = bounded_y - static_cast<float>(lower_y);
    const auto at = [&](std::uint32_t cell_x, std::uint32_t cell_y) {
        return values[static_cast<std::size_t>(cell_y) * kGridWidth + cell_x];
    };
    const float lower = std::lerp(at(lower_x, lower_y), at(upper_x, lower_y), fraction_x);
    const float upper = std::lerp(at(lower_x, upper_y), at(upper_x, upper_y), fraction_x);
    return std::lerp(lower, upper, fraction_y);
}

struct DryNeighbor {
    std::uint32_t dry_x = 0U;
    std::uint32_t y = 0U;
    float seed_difference = 0.0F;
};

DryNeighbor choose_dry_neighbor() {
    DryNeighbor best{};
    for (std::uint32_t y = 1U; y + 1U < kGridHeight; ++y) {
        for (std::uint32_t dry_x = 2U; dry_x + 2U < kGridWidth; ++dry_x) {
            const float dry_seed = fluid_25d_presentation_cue_seed(dry_x, y, true);
            const float wet_seed = fluid_25d_presentation_cue_seed(dry_x + 1U, y, true);
            const float difference = std::abs(dry_seed - wet_seed);
            if (difference > best.seed_difference)
                best = {dry_x, y, difference};
        }
    }
    require(best.seed_difference > 0.005F, "dry-column support",
            "native cue seeds do not separate the wet-only and dry-inclusive samples enough");
    return best;
}

} // namespace

void validate_fluid_25d_native_cue_gpu_controls(cubey::vulkan::Device& device,
                                                cubey::ProjectGpuServices& gpu) {
    Fluid25DConfig config;
    config.scenario = Fluid25DScenario::DryBed;
    config.grid_width = kGridWidth;
    config.grid_height = kGridHeight;
    config.cell_size_m = kCellSizeM;
    config.fixed_delta_seconds = 1.0F;
    config.minimum_wet_depth_m = 0.0001F;
    const Fluid25DScenarioData scenario =
        make_fluid_25d_scenario(Fluid25DScenario::DryBed, kGridWidth, kGridHeight, kCellSizeM);

    Fluid25DGpuResources resources;
    struct Cleanup {
        Fluid25DGpuResources& resources;
        ~Cleanup() {
            resources.destroy_all_resources();
        }
    };
    [[maybe_unused]] Cleanup cleanup{resources};
    resources.create_global_resources_if_needed(device, gpu, config, scenario, 1U, true);

    std::vector<float> wet_depth(kCellCount, 1.0F);
    const auto terrain_bytes = bytes_of(scenario.terrain_height_m);
    std::vector<std::uint8_t> current_depth_bytes;
    std::vector<std::uint8_t> current_velocity_bytes;
    const auto make_velocity = [](float vx, float vy) {
        return std::vector<Fluid25DVelocityGpu>(
            kCellCount, Fluid25DVelocityGpu{.velocity_wet = {vx, vy, 1.0F, 0.0F}});
    };

    const auto upload_fields = [&](const std::vector<float>& depth,
                                   const std::vector<Fluid25DVelocityGpu>& velocity,
                                   const char* label) {
        require(depth.size() == kCellCount && velocity.size() == kCellCount, label,
                "test upload field has an unexpected cell count");
        static_cast<void>(gpu.submit_and_wait(
            {.label = label, .work = [&](cubey::vulkan::GpuOwnerContext& context) {
                 cubey::vulkan::ImmediateCommands commands(context);
                 resources.record_recording_upload(commands.command_buffer(), 0U, depth, velocity);
                 commands.submit_and_wait();
             }}));
        current_depth_bytes = bytes_of(depth);
        current_velocity_bytes = bytes_of(velocity);
        require_hydraulic_bytes(read_hydraulic_snapshot(gpu, resources), terrain_bytes,
                                current_depth_bytes, current_velocity_bytes, label);
    };

    const auto record_presentation = [&](cubey::vulkan::ImmediateCommands& commands,
                                         float delta_seconds, bool& reset_requested) {
        bool quiver_reset_requested = false;
        record_fluid_25d_recorded_presentation(commands.command_buffer(), resources, config,
                                               delta_seconds, reset_requested,
                                               quiver_reset_requested, false, true);
        require(!quiver_reset_requested, "presentation recorder",
                "unexpected quiver reset request");
    };

    const auto run_submission = [&](const std::vector<float>& deltas, bool reset_cues,
                                    const char* label) {
        const HydraulicSnapshot before = read_hydraulic_snapshot(gpu, resources);
        require_hydraulic_bytes(before, terrain_bytes, current_depth_bytes, current_velocity_bytes,
                                label);
        static_cast<void>(gpu.submit_and_wait(
            {.label = label,
             .work = [&, deltas, reset_cues](cubey::vulkan::GpuOwnerContext& context) {
                 cubey::vulkan::ImmediateCommands commands(context);
                 bool reset_requested = reset_cues;
                 if (reset_requested)
                     record_presentation(commands, 0.0F, reset_requested);
                 for (const float delta : deltas)
                     record_presentation(commands, delta, reset_requested);
                 commands.submit_and_wait();
             }}));
        const HydraulicSnapshot after = read_hydraulic_snapshot(gpu, resources);
        require(after == before, label,
                "cue dispatch changed scratch terrain, depth, or velocity bytes");
    };

    const auto current_cues = [&] {
        return std::array<std::vector<std::uint8_t>, 2>{
            read_buffer(gpu, resources.presentation_cue_a(), "native cue A readback"),
            read_buffer(gpu, resources.presentation_cue_b(), "native cue B readback"),
        };
    };

    const auto verify_seeded_cues = [&](const char* control) {
        const auto cues = current_cues();
        require_cue_equal(cues[0], cues[1], control, "reset did not seed cue A and B identically");
        const auto actual = decode_floats(cues[0], kCellCount, control);
        for (std::uint32_t y = 0U; y < kGridHeight; ++y) {
            for (std::uint32_t x = 0U; x < kGridWidth; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * kGridWidth + x;
                const float expected = fluid_25d_presentation_cue_seed(x, y, true);
                require(std::isfinite(actual[index]) &&
                            std::abs(actual[index] - expected) <= kCueTolerance,
                        control, "GPU reset differs from the native CPU cue seed");
            }
        }
        require(resources.current_presentation_cue_is_a(), control,
                "reset did not restore cue A parity");
        return cues[0];
    };

    const auto set_status = [&](std::uint32_t value, const char* label) {
        const auto& status = resources.presentation_cue_status();
        static_cast<void>(gpu.submit_and_wait(
            {.label = label, .work = [&](cubey::vulkan::GpuOwnerContext& context) {
                 cubey::vulkan::ImmediateCommands commands(context);
                 const VkCommandBuffer command_buffer = commands.command_buffer();
                 cubey::vulkan::record_memory_barrier(
                     command_buffer,
                     {
                         .src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         .dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                         .src_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         .dst_access = VK_ACCESS_TRANSFER_WRITE_BIT,
                     });
                 vkCmdFillBuffer(command_buffer, status.handle(), 0U, status.size(), value);
                 cubey::vulkan::record_transfer_write_barrier(command_buffer,
                                                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                              VK_ACCESS_SHADER_READ_BIT);
                 commands.submit_and_wait();
             }}));
    };

    const auto initial_velocity = make_velocity(0.0F, 0.0F);
    upload_fields(wet_depth, initial_velocity, "native cue initial recording upload");

    run_submission({}, true, "native cue seed reset");
    const auto initial_seed_bytes = verify_seeded_cues("native cue seed reset");

    // A paused zero-time call records no cue dispatch and must not perturb
    // either buffer or the A/B source parity.
    run_submission({0.0F}, false, "native cue zero-time pause");
    auto cues = current_cues();
    require_cue_equal(cues[0], initial_seed_bytes, "zero-time pause",
                      "zero-time pause changed cue A");
    require_cue_equal(cues[1], initial_seed_bytes, "zero-time pause",
                      "zero-time pause changed cue B");
    require(resources.current_presentation_cue_is_a(), "zero-time pause",
            "zero-time pause advanced cue parity");

    // A zero velocity still dispatches, copying each source value to its
    // destination without relaxing toward a new procedural pattern.
    run_submission({1.0F}, false, "native cue zero velocity freeze");
    cues = current_cues();
    require_cue_equal(cues[0], initial_seed_bytes, "zero velocity", "zero velocity changed cue A");
    require_cue_equal(cues[1], initial_seed_bytes, "zero velocity", "zero velocity changed cue B");
    require(!resources.current_presentation_cue_is_a(), "zero velocity",
            "zero-velocity dispatch did not advance cue parity");

    // A 7.5 m/s held velocity at 30 m cells backtraces by exactly 0.25 cell
    // over one second. Compare one interior result against a separately
    // calculated bilinear sample of the actual GPU-reset seed plus relaxation.
    const auto quarter_cell_velocity = make_velocity(7.5F, 0.0F);
    upload_fields(wet_depth, quarter_cell_velocity, "native cue quarter-cell upload");
    run_submission({}, true, "native cue quarter-cell reset");
    const auto quarter_seed_bytes = verify_seeded_cues("quarter-cell reset");
    const auto quarter_seed = decode_floats(quarter_seed_bytes, kCellCount, "quarter-cell seed");
    std::uint32_t sample_x = 0U;
    std::uint32_t sample_y = 0U;
    float expected_sample = 0.0F;
    float sample_change = 0.0F;
    for (std::uint32_t y = 2U; y + 2U < kGridHeight; ++y) {
        for (std::uint32_t x = 2U; x + 2U < kGridWidth; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * kGridWidth + x;
            const float advected =
                bilinear(quarter_seed, static_cast<float>(x) - 0.25F, static_cast<float>(y));
            const float expected =
                std::clamp(std::lerp(advected, quarter_seed[index], 0.0015F), 0.0F, 1.0F);
            const float change = std::abs(expected - quarter_seed[index]);
            if (change > sample_change) {
                sample_x = x;
                sample_y = y;
                expected_sample = expected;
                sample_change = change;
            }
        }
    }
    require(sample_change > 1.0e-5F, "quarter-cell advection",
            "selected GPU seed has no measurable interior displacement");
    run_submission({1.0F}, false, "native cue quarter-cell advection");
    cues = current_cues();
    const auto quarter_output = decode_floats(cues[1], kCellCount, "quarter-cell output");
    const std::size_t quarter_index = static_cast<std::size_t>(sample_y) * kGridWidth + sample_x;
    require(std::abs(quarter_output[quarter_index] - expected_sample) <= kCueTolerance,
            "quarter-cell advection",
            "GPU output differs from bilinear seed advection plus 0.0015 relaxation");
    require(cues[0] == quarter_seed_bytes, "quarter-cell advection",
            "advection modified its GPU seed source buffer");
    require(!resources.current_presentation_cue_is_a(), "quarter-cell advection",
            "quarter-cell dispatch did not advance cue parity");

    // At a 0.5-cell backtrace, the wet neighbor alone supplies the normalized
    // sample. A dynamically selected dry/wet pair ensures that including the
    // dry cell's independent seed would be observably different.
    const DryNeighbor dry_neighbor = choose_dry_neighbor();
    std::vector<float> dry_column_depth = wet_depth;
    for (std::uint32_t y = 0U; y < kGridHeight; ++y)
        dry_column_depth[static_cast<std::size_t>(y) * kGridWidth + dry_neighbor.dry_x] = 0.0F;
    const auto half_cell_velocity = make_velocity(15.0F, 0.0F);
    upload_fields(dry_column_depth, half_cell_velocity, "native cue dry-column upload");
    run_submission({}, true, "native cue dry-column reset");
    const auto dry_seed_bytes = verify_seeded_cues("dry-column reset");
    const auto dry_seed = decode_floats(dry_seed_bytes, kCellCount, "dry-column seed");
    run_submission({1.0F}, false, "native cue dry-column advection");
    cues = current_cues();
    const auto dry_output = decode_floats(cues[1], kCellCount, "dry-column output");
    for (std::uint32_t y = 0U; y < kGridHeight; ++y) {
        const std::size_t index = static_cast<std::size_t>(y) * kGridWidth + dry_neighbor.dry_x;
        require(std::abs(dry_output[index] - dry_seed[index]) <= kCueTolerance,
                "dry-column support", "dry cell did not remain at its native seed");
    }
    const std::size_t dry_index =
        static_cast<std::size_t>(dry_neighbor.y) * kGridWidth + dry_neighbor.dry_x;
    const std::size_t wet_index = dry_index + 1U;
    const float wet_only_expected =
        std::clamp(std::lerp(dry_seed[wet_index], dry_seed[wet_index], 0.0015F), 0.0F, 1.0F);
    const float dry_inclusive_expected = std::clamp(
        std::lerp(0.5F * (dry_seed[dry_index] + dry_seed[wet_index]), dry_seed[wet_index], 0.0015F),
        0.0F, 1.0F);
    require(std::abs(dry_output[wet_index] - wet_only_expected) <= kCueTolerance,
            "dry-column support", "wet neighbor did not use wet-only normalized support");
    require(std::abs(dry_output[wet_index] - dry_inclusive_expected) > 0.001F, "dry-column support",
            "dry seed still contributes to the wet-neighbor sample");

    // A displacement greater than the native half-cell support freezes the
    // original wet cue bytes instead of fabricating a long-range animation.
    upload_fields(wet_depth, make_velocity(31.0F, 0.0F), "native cue unsupported-jump upload");
    run_submission({}, true, "native cue unsupported-jump reset");
    const auto unsupported_seed_bytes = verify_seeded_cues("unsupported-jump reset");
    run_submission({1.0F}, false, "native cue unsupported-jump freeze");
    cues = current_cues();
    require_cue_equal(cues[0], unsupported_seed_bytes, "unsupported displacement",
                      "unsupported displacement changed cue A");
    require_cue_equal(cues[1], unsupported_seed_bytes, "unsupported displacement",
                      "unsupported displacement changed cue B");

    // Any latched status flag is a hard presentation freeze. Fill and clear
    // the dedicated scratch status buffer with explicit transfer barriers.
    upload_fields(wet_depth, make_velocity(7.5F, 0.0F), "native cue status-freeze upload");
    run_submission({}, true, "native cue status-freeze reset");
    const auto status_seed_bytes = verify_seeded_cues("status-freeze reset");
    set_status(1U, "native cue set status flag");
    run_submission({1.0F}, false, "native cue status freeze");
    cues = current_cues();
    require_cue_equal(cues[0], status_seed_bytes, "nonzero status flag",
                      "status freeze changed cue A");
    require_cue_equal(cues[1], status_seed_bytes, "nonzero status flag",
                      "status freeze changed cue B");
    set_status(0U, "native cue clear status flag");

    // Reset determinism and command batching: identical reset plus four
    // quarter-second updates must produce identical bytes whether the updates
    // share one command submission or are submitted individually.
    const std::vector<float> quarter_steps(4U, 0.25F);
    run_submission(quarter_steps, true, "native cue batched replay");
    const auto batched = current_cues();
    run_submission({}, true, "native cue split replay reset");
    const auto replay_seed_bytes = verify_seeded_cues("split replay reset");
    require_cue_equal(replay_seed_bytes, status_seed_bytes, "reset determinism",
                      "repeated reset did not reproduce the original seed bytes");
    for (const float delta : quarter_steps)
        run_submission({delta}, false, "native cue split replay step");
    const auto split = current_cues();
    require_cue_equal(batched[0], split[0], "batch determinism",
                      "batched and separately submitted cue A differ");
    require_cue_equal(batched[1], split[1], "batch determinism",
                      "batched and separately submitted cue B differ");
    run_submission(quarter_steps, true, "native cue repeated batch replay");
    const auto repeated = current_cues();
    require_cue_equal(batched[0], repeated[0], "batch determinism",
                      "repeated batched cue A differs");
    require_cue_equal(batched[1], repeated[1], "batch determinism",
                      "repeated batched cue B differs");

    std::printf("fluid_25d_native_cue_controls: PASS reset-seed zero-time-freeze "
                "zero-velocity-freeze quarter-cell-advection dry-column-support "
                "unsupported-displacement status-freeze reset-determinism batch-determinism "
                "hydraulic-byte-preservation\n");
    std::fflush(stdout);
}

} // namespace cubey::projects::fluid::fluid_25d
