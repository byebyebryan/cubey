#include "sdf_validation.h"

#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
struct Ray {
    math::Vec4 origin, direction;
};
float reference_trace(const Ray& ray, const Recipe& recipe, float threshold) {
    float t = 0;
    const bool inside = recipe_distance(math::Vec3(ray.origin), recipe.edits) < 0;
    for (unsigned step = 0; step < 16384 && t < 3.0F; ++step) {
        const float d =
            (inside ? -1.0F : 1.0F) *
            recipe_distance(math::Vec3(ray.origin) + math::Vec3(ray.direction) * t, recipe.edits);
        if (d < threshold)
            return t;
        t += std::max((d - threshold) * 0.9F, 1.0e-6F);
    }
    if (t < 3.0F)
        throw std::runtime_error("SDF CPU oracle exhausted its iterations");
    return -1;
}
} // namespace
void validate_field(vulkan::Device& device, ProjectGpuServices& gpu, const Field& field,
                    const Recipe& recipe) {
    std::vector<Ray> rays;
    for (float yaw : {0.0F, 0.8F, 2.5F}) {
        const math::Vec3 eye = math::Vec3{std::sin(yaw), 0.3F, std::cos(yaw)} * 1.4F;
        const auto forward = glm::normalize(-eye);
        const auto right = glm::normalize(glm::cross(forward, math::Vec3{0, 1, 0}));
        const auto up = glm::cross(right, forward);
        for (int y = -4; y <= 4; ++y)
            for (int x = -4; x <= 4; ++x) {
                const auto target =
                    right * (static_cast<float>(x) * 0.1F) + up * (static_cast<float>(y) * 0.1F);
                rays.push_back({{eye, 0}, {glm::normalize(target - eye), 0}});
            }
    }
    // Exact axis-parallel and brick-plane rays exercise the DDA tie rules.
    for (float x : {-0.25F, 0.0F, 0.125F, 0.25F})
        rays.push_back({{x, 0, 1.2F, 0}, {0, 0, -1, 0}});
    if (!recipe.edits.empty()) {
        const auto center = recipe.edits.back().center;
        rays.push_back({{center + math::Vec3{0, 0, 1.2F}, 0}, {0, 0, -1, 0}});
    }
    // Small fixtures can check every edit, including all edits either side of
    // a promotion boundary, rather than relying on their chance visibility.
    if (recipe.edits.size() <= 257)
        for (const auto& edit : recipe.edits)
            rays.push_back({{edit.center + math::Vec3{0, 0, 1.2F}, 0}, {0, 0, -1, 0}});
    for (auto direction : {math::Vec3{1, 0, 0}, math::Vec3{0, 1, 0}, math::Vec3{0, 0, 1},
                           glm::normalize(math::Vec3{1, 0.001F, 1})}) {
        rays.push_back({{0, 0, 0, 0}, {direction, 0}});
        if (!recipe.edits.empty())
            rays.push_back({{recipe.edits.front().center, 0}, {direction, 0}});
    }
    auto ray_buffer =
        gpu.upload_device_buffer<Ray>(rays, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "sdf oracle rays");
    vulkan::Buffer result_buffer(
        device, vulkan::device_local_buffer_config(rays.size() * sizeof(math::Vec4),
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    const std::array<vulkan::DescriptorSetBindingConfig, 4> bindings{
        {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
         {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
         {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
         {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}}};
    vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
    vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(descriptors.set(), 0, field.nodes().handle(), field.nodes().size());
    writes.combined_image_sampler(descriptors.set(), 1, field.atlas().sampler().handle(),
                                  field.atlas().view(), VK_IMAGE_LAYOUT_GENERAL);
    writes.storage_buffer(descriptors.set(), 2, ray_buffer.handle(), ray_buffer.size());
    writes.storage_buffer(descriptors.set(), 3, result_buffer.handle(), result_buffer.size());
    writes.update(device);
    const std::array layouts{descriptors.layout()};
    const std::array<VkPushConstantRange, 1> push{{{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8}}};
    const render::ComputePipelineResource pipeline(
        device, {.shader_stage = render::compute_shader_file(
                     std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "validate.comp.spv"),
                 .descriptor_set_layouts = layouts,
                 .push_constants = push});
    static_cast<void>(gpu.submit_and_wait(
        {.label = "sdf validate ray tracing", .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             const vulkan::CommandRecorder recorder(commands.command_buffer());
             recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
             recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                          descriptors.set());
             recorder.push_constants(
                 pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                 std::array<std::uint32_t, 2>{field.atlas_axis,
                                              static_cast<std::uint32_t>(rays.size())});
             recorder.dispatch(static_cast<std::uint32_t>((rays.size() + 63) / 64), 1, 1);
             commands.submit_and_wait();
         }}));
    const auto bytes =
        gpu.readback_buffer(result_buffer.handle(), result_buffer.size(), "sdf oracle results");
    std::uint32_t mismatches = 0, exhausted = 0, hits = 0, ambiguous = 0;
    float max_error = 0, max_surface_error = 0;
    for (std::size_t i = 0; i < rays.size(); ++i) {
        math::Vec4 result;
        std::memcpy(&result, bytes.data() + i * sizeof(result), sizeof(result));
        constexpr float voxel = 1.0F / (256.0F * 7.0F);
        constexpr float iso = 0.2F * voxel;
        // Trilinear interpolation of a 1-Lipschitz box CSG field has a
        // sqrt(3)/2 sample-spacing error bound. Add 0.01 sample for R16F.
        constexpr float bound = 0.8761F * voxel;
        const float expected = reference_trace(rays[i], recipe, iso);
        const float outer = reference_trace(rays[i], recipe, iso + bound);
        const float inner = reference_trace(rays[i], recipe, iso - bound);
        if (result.y > 1.5F) {
            ++exhausted;
            continue;
        }
        if (result.y > 0.5F) {
            ++hits;
            const float error = expected < 0 ? 1000.0F : std::abs(expected - result.x);
            if (expected >= 0)
                max_error = std::max(max_error, error);
            const auto point =
                math::Vec3(rays[i].origin) + math::Vec3(rays[i].direction) * result.x;
            const float surface_error = std::abs(recipe_distance(point, recipe.edits));
            max_surface_error = std::max(max_surface_error, surface_error);
            // A spatial sampling tolerance translates to a larger ray distance
            // at grazing incidence. Bound both, rather than hiding depth errors
            // behind an unconditional surface-distance check.
            math::Vec3 gradient{0};
            const auto expected_point =
                math::Vec3(rays[i].origin) + math::Vec3(rays[i].direction) * expected;
            for (int axis = 0; axis < 3; ++axis) {
                auto a = expected_point, b = expected_point;
                a[axis] -= 1e-5F;
                b[axis] += 1e-5F;
                gradient[axis] =
                    recipe_distance(b, recipe.edits) - recipe_distance(a, recipe.edits);
            }
            const float incidence = std::max(
                0.02F, std::abs(glm::dot(glm::normalize(gradient), math::Vec3(rays[i].direction))));
            constexpr float tolerance = 2.0F / (256.0F * 7.0F);
            const bool outside_bracket = outer < 0 || result.x < outer - voxel ||
                                         (inner >= 0 && result.x > inner + voxel) ||
                                         surface_error > iso + bound;
            if (outside_bracket) {
                ++mismatches;
                std::cout << "sdf: oracle mismatch ray=" << i << " expected_t=" << expected
                          << " actual_t=" << result.x << " surface_error=" << surface_error << '\n';
            } else if (expected < 0 || error > tolerance / incidence)
                ++ambiguous;
        } else if (inner >= 0)
            ++mismatches;
        else if (expected >= 0)
            ++ambiguous;
    }
    std::cout << "sdf: oracle rays=" << rays.size() << " hits=" << hits
              << " mismatches=" << mismatches << " exhausted=" << exhausted
              << " max_hit_error=" << max_error << " max_surface_error=" << max_surface_error
              << " sampling_ambiguous=" << ambiguous << '\n';
    if (mismatches || exhausted)
        throw std::runtime_error("SDF GPU/CPU ray validation failed");
}
void validate_batches(vulkan::Device& device, ProjectGpuServices& gpu, Field& field) {
    Recipe last;
    for (unsigned count : {0U, 128U, 129U, 256U, 257U, 257U, 129U, 128U, 0U, 257U}) {
        Recipe recipe;
        for (unsigned i = 0; i < count; ++i) {
            BoxEdit edit;
            edit.center = {-0.24F + static_cast<float>(i % 17U) * 0.03F,
                           -0.225F + static_cast<float>(i / 17U) * 0.03F, 0};
            edit.half_extent = math::Vec3{0.006F};
            edit.rotation = math::euler_xyz_quat({0.1F, 0.2F, 0.3F});
            recipe.edits.push_back(edit);
        }
        field.build(recipe.edits);
        if (field.displayed_edit_count() != recipe.edits.size())
            throw std::runtime_error("SDF cache published the wrong edit prefix");
        validate_field(device, gpu, field, recipe);
        last = recipe;
    }
    {
        Field smaller_batches(device, gpu, 64, 64);
        smaller_batches.build(last.edits);
        validate_field(device, gpu, smaller_batches, last);
    }
    for (bool reference_overflow : {false, true}) {
        Field limited(device, gpu, 64, 128, reference_overflow ? 0U : 16U,
                      reference_overflow ? 128U : 16U * 1024U * 1024U);
        bool rejected = false;
        try {
            limited.build(make_recipe("box", 0, 1).edits);
        } catch (const std::runtime_error& error) {
            if (std::string_view(error.what()).find("SDF field capacity exceeded") ==
                std::string_view::npos)
                throw;
            rejected = true;
        }
        if (!rejected || limited.displayed_edit_count() != 0)
            throw std::runtime_error("SDF capacity failure was silently published");
        limited.build({});
        validate_field(device, gpu, limited, Recipe{});
    }
    std::cout << "sdf: batch/reset matrix passed (forward, unchanged, backward, reset, 64/128 edit "
                 "batches, frontier/reference overflow recovery)\n";
}
} // namespace cubey::projects::sdf
