#include "sdf_validation.h"
#include "sdf_history.h"
#include "sdf_material.h"

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
template <class Distance>
float reference_trace(const Ray& ray, const Distance& distance, float threshold) {
    float t = 0;
    const bool inside = distance(math::Vec3(ray.origin)) < 0;
    for (unsigned step = 0; step < 16384 && t < 3.0F; ++step) {
        const float d = (inside ? -1.0F : 1.0F) *
                        distance(math::Vec3(ray.origin) + math::Vec3(ray.direction) * t);
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
                    const Recipe& recipe, const AuthoredScene* authored) {
    const auto distance = [&](math::Vec3 p) {
        return authored ? authored_distance(p, *authored) : recipe_distance(p, recipe.edits);
    };
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
    if (authored) {
        for (const auto& node : authored->program) {
            if (node.center_code.w >= 10)
                continue;
            const auto center = math::Vec3(node.center_code);
            rays.push_back({{center + math::Vec3{0, 0, 1.2F}, 0}, {0, 0, -1, 0}});
            for (const auto axis : {math::Vec3{1, 0, 0}, math::Vec3{0, 1, 0}, math::Vec3{0, 0, 1}})
                rays.push_back({{center, 0}, {axis, 0}});
        }
    }
    const auto probe_edit = [&](const BoxEdit& edit) {
        rays.push_back({{edit.center + math::Vec3{0, 0, 1.2F}, 0}, {0, 0, -1, 0}});
        if (edit.operation == Operation::Remove) {
            // Begin in a carved cavity, then query along and across its axes.
            for (const auto axis : {math::Vec3{1, 0, 0}, math::Vec3{0, 1, 0}, math::Vec3{0, 0, 1}})
                rays.push_back({{edit.center, 0}, {glm::normalize(edit.rotation) * axis, 0}});
        }
    };
    // Small fixtures check every edit across batch promotion boundaries.
    if (recipe.edits.size() <= 257) {
        for (const auto& edit : recipe.edits)
            probe_edit(edit);
    } else {
        // Exterior grids rarely hit slender bores. Probe a bounded sample of
        // actual removals across the full recipe, including its first and last.
        std::vector<const BoxEdit*> cutters;
        for (const auto& edit : recipe.edits)
            if (edit.operation == Operation::Remove)
                cutters.push_back(&edit);
        const auto samples = std::min<std::size_t>(64, cutters.size());
        for (std::size_t i = 0; i < samples; ++i) {
            const auto index = samples == 1 ? 0 : i * (cutters.size() - 1) / (samples - 1);
            probe_edit(*cutters[index]);
        }
    }
    // These off-center origins exercise remaining solid in the subtraction
    // fixtures, in addition to the center origins that become empty cavities.
    for (const auto origin : {math::Vec3{0.249F, 0.232F, 0}, math::Vec3{0.17F, 0.15F, 0.025F}})
        rays.push_back({{origin, 0}, {glm::normalize(math::Vec3{1, 0.001F, 1}), 0}});
    for (auto direction : {math::Vec3{1, 0, 0}, math::Vec3{0, 1, 0}, math::Vec3{0, 0, 1},
                           glm::normalize(math::Vec3{1, 0.001F, 1})}) {
        rays.push_back({{0, 0, 0, 0}, {direction, 0}});
        if (!recipe.edits.empty())
            rays.push_back({{recipe.edits.front().center, 0}, {direction, 0}});
    }
    if (authored && authored->optics.size() == 1 && authored->optics[0].settings.y > 0) {
        const auto count = std::min<std::size_t>(rays.size(), 243);
        constexpr float voxel = 1.0F / (256 * 7);
        for (std::size_t i = 0; i < count; ++i) {
            const float t = reference_trace(rays[i], distance, voxel * .2F);
            if (t < 0)
                continue;
            const auto point = math::Vec3{rays[i].origin} + math::Vec3{rays[i].direction} * t;
            math::Vec3 gradient;
            for (int axis = 0; axis < 3; ++axis) {
                auto a = point, b = point;
                a[axis] -= voxel * .5F;
                b[axis] += voxel * .5F;
                gradient[axis] = distance(b) - distance(a);
            }
            if (glm::length(gradient) < 1e-8F)
                continue;
            const auto normal = glm::normalize(gradient);
            const auto face = optical_interface(math::Vec3{rays[i].direction}, normal, false,
                                                authored->optics[0].settings.z);
            if (!face.tir)
                rays.push_back({{point - normal * voxel, 0}, {face.transmitted, 0}});
        }
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
        const float expected = reference_trace(rays[i], distance, iso);
        const float outer = reference_trace(rays[i], distance, iso + bound);
        const float inner = reference_trace(rays[i], distance, iso - bound);
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
            const float surface_error = std::abs(distance(point));
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
                gradient[axis] = distance(b) - distance(a);
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
        limited.build(std::span<const BoxEdit>{});
        validate_field(device, gpu, limited, Recipe{});
    }
    std::cout << "sdf: batch/reset matrix passed (forward, unchanged, backward, reset, 64/128 edit "
                 "batches, frontier/reference overflow recovery)\n";
}

void validate_subtractions(vulkan::Device& device, ProjectGpuServices& gpu, Field& field) {
    Recipe crossing;
    BoxEdit base;
    base.half_extent = {0.22F, 0.20F, 0.04F};
    crossing.edits.push_back(base);
    BoxEdit bore;
    bore.operation = Operation::Remove;
    bore.half_extent = {0.04F, 0.03F, 0.30F};
    crossing.edits.push_back(bore);
    bore.half_extent = {0.30F, 0.04F, 0.02F};
    crossing.edits.push_back(bore);
    field.build(crossing.edits);
    validate_field(device, gpu, field, crossing);

    // A perforated plate gives each late cut a separate, visible location.
    // Unlike hundreds of random cuts, this cannot pass through redundant tails.
    Recipe last;
    for (unsigned count : {1U, 128U, 129U, 256U, 257U, 129U, 1U, 0U, 257U}) {
        Recipe recipe;
        base.half_extent = {0.255F, 0.24F, 0.015F};
        if (count > 0)
            recipe.edits.push_back(base);
        for (unsigned i = 1; i < count; ++i) {
            BoxEdit cut;
            cut.operation = Operation::Remove;
            cut.center = {-0.24F + static_cast<float>((i - 1) % 17U) * 0.03F,
                          -0.225F + static_cast<float>((i - 1) / 17U) * 0.03F, 0};
            cut.half_extent = {0.005F, 0.005F, 0.10F};
            recipe.edits.push_back(cut);
        }
        field.build(recipe.edits);
        if (field.displayed_edit_count() != recipe.edits.size())
            throw std::runtime_error("Subtractive cache published the wrong edit prefix");
        validate_field(device, gpu, field, recipe);
        last = recipe;
    }
    Field smaller_batches(device, gpu, 64, 64);
    smaller_batches.build(last.edits);
    validate_field(device, gpu, smaller_batches, last);
    std::cout << "sdf: subtraction matrix passed (intersecting cavities, visible late cuts, "
                 "128/129/256/257 total edits, backward/reset, 64/128 batches)\n";
}
void validate_authored(vulkan::Device& device, ProjectGpuServices& gpu,
                       const AuthoredScene& scene) {
    std::vector<math::Vec4> points;
    for (int x = -4; x <= 4; ++x)
        for (int y = -4; y <= 4; ++y)
            for (int z = -4; z <= 4; ++z)
                points.push_back({static_cast<float>(x) * .1F, static_cast<float>(y) * .1F,
                                  static_cast<float>(z) * .1F, 0});
    for (const auto& node : scene.program) {
        if (node.center_code.w >= 10)
            continue;
        points.push_back({math::Vec3(node.center_code), 0});
        for (const auto axis : {math::Vec3{1, 0, 0}, math::Vec3{0, 1, 0}, math::Vec3{0, 0, 1}}) {
            points.push_back({math::Vec3(node.center_code) + axis * .025F, 0});
            points.push_back({math::Vec3(node.center_code) - axis * .025F, 0});
        }
    }
    const auto program = authored_program_bytes(&scene);
    auto program_buffer = gpu.upload_device_buffer<std::uint8_t>(
        program, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "sdf authored program");
    auto materials = gpu.upload_device_buffer<SurfaceMaterial>(
        scene.materials, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "sdf authored materials");
    auto patterns = gpu.upload_device_buffer<MaterialPattern>(
        scene.patterns, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "sdf authored patterns");
    auto optics = gpu.upload_device_buffer<MaterialOptics>(
        scene.optics, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "sdf authored optics");
    for (std::size_t i = 0; i < points.size(); ++i)
        points[i].w = std::array{0.0F, .0005F, .003F, .02F}[i % 4];
    auto point_buffer = gpu.upload_device_buffer<math::Vec4>(
        points, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "sdf authored points");
    vulkan::Buffer results(
        device, vulkan::device_local_buffer_config(points.size() * 10 * sizeof(math::Vec4),
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    const std::array<vulkan::DescriptorSetBindingConfig, 6> bindings{
        {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
         {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
         {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
         {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
         {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
         {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}}};
    vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
    vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(descriptors.set(), 0, program_buffer.handle(), program_buffer.size());
    writes.storage_buffer(descriptors.set(), 1, materials.handle(), materials.size());
    writes.storage_buffer(descriptors.set(), 2, point_buffer.handle(), point_buffer.size());
    writes.storage_buffer(descriptors.set(), 3, results.handle(), results.size());
    writes.storage_buffer(descriptors.set(), 4, patterns.handle(), patterns.size());
    writes.storage_buffer(descriptors.set(), 5, optics.handle(), optics.size());
    writes.update(device);
    const std::array layouts{descriptors.layout()};
    const std::array<VkPushConstantRange, 1> push{{{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4}}};
    const render::ComputePipelineResource pipeline(
        device, {.shader_stage = render::compute_shader_file(
                     std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "authored_validate.comp.spv"),
                 .descriptor_set_layouts = layouts,
                 .push_constants = push});
    static_cast<void>(gpu.submit_and_wait(
        {.label = "sdf authored CPU GPU check", .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             const vulkan::CommandRecorder recorder(commands.command_buffer());
             recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
             recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                          descriptors.set());
             recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     static_cast<std::uint32_t>(points.size()));
             recorder.dispatch(static_cast<std::uint32_t>((points.size() + 63) / 64), 1, 1);
             commands.submit_and_wait();
         }}));
    const auto bytes =
        gpu.readback_buffer(results.handle(), results.size(), "sdf authored results");
    // Thin procedural vein ramps amplify ordinary CPU/GPU noise/trig rounding.
    // Keep geometry/normal bounds unchanged and retain the tighter plain bound.
    const float material_tolerance = std::any_of(scene.patterns.begin(), scene.patterns.end(),
                                                 [](const auto& p) { return p.settings.x != 0; })
                                         ? 2e-4F
                                         : 2e-5F;
    float max_distance = 0, max_material = 0, max_gradient = 0, max_normal = 0, max_fast = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        std::array<math::Vec4, 10> actual;
        std::memcpy(actual.data(), bytes.data() + i * sizeof(actual), sizeof(actual));
        for (const auto& lane : actual)
            for (int channel = 0; channel < 4; ++channel)
                if (!std::isfinite(lane[channel]))
                    throw std::runtime_error("authored GPU evaluation returned a nonfinite value");
        const auto expected = authored_surface(math::Vec3(points[i]), scene, points[i].w);
        const auto rough_light = glm::normalize(math::Vec3{points[i].x, points[i].y, 1});
        const auto rough_view = glm::normalize(math::Vec3{-points[i].y, points[i].z, 1});
        if (std::abs(actual[3].w - rough_diffuse_factor(rough_light.z, rough_view.z,
                                                        glm::dot(rough_light, rough_view),
                                                        expected.optics.attenuation.w)) > 2e-5F)
            throw std::runtime_error("CPU/GPU rough diffuse mismatch");
        max_distance = std::max({max_distance, std::abs(actual[0].x - expected.distance),
                                 std::abs(actual[0].y - expected.distance)});
        for (int channel = 0; channel < 4; ++channel)
            max_material =
                std::max({max_material,
                          std::abs(actual[1][channel] - expected.material.base_roughness[channel]),
                          std::abs(actual[2][channel] - expected.material.emission[channel])});
        const auto normal = material_shading_normal(glm::normalize(math::Vec3{.2F, 1, .3F}),
                                                    expected.height_gradient);
        for (int channel = 0; channel < 3; ++channel) {
            max_gradient = std::max(
                max_gradient, std::abs(actual[3][channel] - expected.height_gradient[channel]));
            max_normal = std::max(max_normal, std::abs(actual[4][channel] - normal[channel]));
        }
        for (int channel = 0; channel < 4; ++channel)
            if (std::abs(actual[5][channel] - expected.optics.settings[channel]) > 2e-5F ||
                std::abs(actual[6][channel] - expected.optics.attenuation[channel]) > 2e-5F)
                throw std::runtime_error("authored CPU/GPU optics mismatch");
        const bool optical_inside = points[i].y > 0;
        const auto optical =
            optical_interface(glm::normalize(math::Vec3{points[i].x * 3, .2F, -1}),
                              optical_inside ? math::Vec3{0, 0, -1} : math::Vec3{0, 0, 1},
                              optical_inside, expected.optics.settings.z);
        if (glm::length(math::Vec3{actual[7]} - optical.transmitted) > 2e-5F ||
            glm::length(math::Vec3{actual[8]} - optical.reflected) > 2e-5F ||
            std::abs(actual[7].w - optical.fresnel) > 2e-5F || (actual[8].w > .5F) != optical.tir)
            throw std::runtime_error("CPU/GPU optical interface mismatch");
        if (glm::length(math::Vec3{actual[9]} - optical_absorption(expected.optics, .37F)) > 2e-5F)
            throw std::runtime_error("CPU/GPU optical absorption mismatch");
        max_fast = std::max(max_fast, actual[0].w);
        if (!std::isfinite(actual[0].x) || !std::isfinite(actual[0].y) ||
            static_cast<unsigned>(actual[0].z) != expected.part || max_distance > 2e-5F ||
            max_material > material_tolerance || max_gradient > 2e-5F || max_normal > 2e-5F ||
            max_fast > 2e-5F)
            throw std::runtime_error(
                "authored CPU/GPU distance/material mismatch at point " + std::to_string(i) +
                " CPU part=" + std::to_string(expected.part) +
                " GPU part=" + std::to_string(static_cast<unsigned>(actual[0].z)) +
                " distance_error=" + std::to_string(max_distance) + " material_error=" +
                std::to_string(max_material) + " gradient_error=" + std::to_string(max_gradient));
    }
    std::cout << "sdf: authored CPU/GPU points=" << points.size()
              << " max_distance_error=" << max_distance << " max_material_error=" << max_material
              << " max_gradient_error=" << max_gradient << " max_normal_error=" << max_normal
              << " max_shortcut_error=" << max_fast << " part_mismatches=0\n";
}
void validate_authored_lifecycle(vulkan::Device& device, ProjectGpuServices& gpu) {
    auto small = parse_authored_scene(R"({"format":"cubey.sdf.scene","version":2,"name":"lifecycle",
        "materials":[{"name":"plain","base_color":[0.5,0.5,0.5]}],
        "root":{"name":"ball","type":"sphere","radius":0.01}})");
    const auto large =
        parse_authored_scene(R"({"format":"cubey.sdf.scene","version":2,"name":"overflow",
        "materials":[{"name":"plain","base_color":[0.5,0.5,0.5]}],
        "root":{"name":"ball","type":"sphere","radius":0.3}})");
    Field limited(device, gpu, 64, 128, 1024);
    limited.build(small);
    const auto handle = limited.nodes().handle();
    const auto stats = limited.stats();
    bool failed = false;
    try {
        limited.build(large);
    } catch (const CapacityExceeded&) {
        failed = true;
    }
    if (!failed || limited.nodes().handle() != handle ||
        limited.stats().brick_count != stats.brick_count ||
        limited.stats().build_count != stats.build_count)
        throw std::runtime_error("authored capacity failure replaced the completed field");
    validate_field(device, gpu, limited, Recipe{}, &small);
    small.materials[0].base_roughness.x = .2F;
    limited.build(small);
    if (limited.stats().build_count != stats.build_count)
        throw std::runtime_error("authored recoloring rebuilt geometry");
    Recipe legacy;
    BoxEdit box;
    box.half_extent = {.006F, .006F, .006F};
    legacy.edits.push_back(box);
    limited.build(legacy.edits);
    validate_field(device, gpu, limited, legacy);
    limited.build(small);
    validate_field(device, gpu, limited, Recipe{}, &small);
    std::cout << "sdf: authored lifecycle passed (capacity rollback, material-only cache, "
                 "legacy/authored transitions)\n";
}
void validate_authored_history(vulkan::Device& device, ProjectGpuServices& gpu, Field& field,
                               const AuthoredScene& scene) {
    if (scene.history.empty())
        throw std::invalid_argument("--validate-history requires a saved authored history");
    const auto check = [&](std::size_t count) {
        const auto state = authored_history_scene(scene, count);
        field.build(state);
        validate_field(device, gpu, field, Recipe{}, &state);
        validate_authored(device, gpu, state);
        const auto before = field.stats().build_count;
        field.build(state);
        if (field.stats().build_count != before)
            throw std::runtime_error("unchanged history state rebuilt the field");
        std::cout << "sdf: history validated step=" << count << "/" << scene.history.size()
                  << " geometry=" << std::hex << authored_geometry_hash(state)
                  << " appearance=" << authored_appearance_hash(state) << std::dec << '\n';
    };
    for (std::size_t count = 0; count <= scene.history.size(); ++count)
        check(count);
    for (auto count : {scene.history.size() / 2, std::size_t{0}, scene.history.size()})
        check(count);
    std::cout << "sdf: authored history passed (all states, backward/reset, cache reuse)\n";
}
} // namespace cubey::projects::sdf
