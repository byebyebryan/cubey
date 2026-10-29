// Independent numerical gate. Does not publish a scene or change renderer admission.
#include "../sdf_prepared_cones.h"
#include <cmath>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>

using namespace cubey;
using namespace cubey::projects::sdf;
using Json = nlohmann::json;
math::Vec3 world_point(math::Vec3 p, const SceneInstruction& n) {
    p *= n.settings.x;
    const math::Vec3 q = -math::Vec3(n.inverse_rotation);
    p += 2.F * glm::cross(q, glm::cross(q, p) + n.inverse_rotation.w * p);
    return p + math::Vec3(n.center_code);
}
Json array(math::Vec3 v) {
    return Json::array({v.x, v.y, v.z});
}
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::invalid_argument("shader-directory guardian-scene receipt.json");
        auto scene = load_authored_scene(argv[2]);
        const auto guardian_count = scene.program.size();
        const auto add = [&](float bottom, float top, float height) {
            SceneInstruction n;
            n.center_code = {0, 0, 0, 7};
            n.inverse_rotation = {0, 0, 0, 1};
            n.parameters = {bottom, top, height * .5F, 0};
            n.settings = {1, 0, 0, 0};
            scene.program.push_back(n);
        };
        for (float bottom : {.0001F, .03F, .2F, .4F})
            for (float height : {.000002F, .00001F, .0002F, .005F, .1F, .5F})
                for (float ratio : {0.F, .25F, .5F, .9F, .99F, .9999F, std::nextafter(1.F, 0.F),
                                    1.F, std::nextafter(1.F, 2.F)})
                    for (float sign : {-1.F, 1.F}) {
                        const float top = bottom + sign * height * ratio;
                        if (top >= 0)
                            add(bottom, top, height);
                    }
        add(0, .1F, .3F);
        add(.1F, 0, .3F);
        std::mt19937 rng(73129);
        std::uniform_real_distribution<float> unit(0, 1);
        for (unsigned i = 0; i < 256; ++i) {
            const float h = .0002F + .2F * unit(rng), bottom = .1F + .2F * unit(rng);
            float ratio = 1;
            for (unsigned ulp = 0; ulp < i % 32 + 1; ++ulp)
                ratio = std::nextafter(ratio, 0.F);
            add(bottom, bottom + h * ratio, h);
        }
        const auto prepared = prepare_cones(scene);
        if (!prepared.matches(scene))
            throw std::runtime_error("fresh cache identity failed");
        auto appearance = scene;
        appearance.materials[0].base_roughness.x += .01F;
        if (!prepared.matches(appearance))
            throw std::runtime_error("appearance cache reuse failed");
        auto changed = scene;
        changed.program.back().parameters.x += .00001F;
        if (prepared.matches(changed))
            throw std::runtime_error("geometry cache invalidation failed");
        std::swap(changed.program.front(), changed.program.back());
        if (prepared.matches(changed))
            throw std::runtime_error("source-order identity failed");
        std::vector<math::Vec4> queries;
        for (unsigned id = 0; id < scene.program.size(); ++id) {
            const auto& n = scene.program[id];
            if (n.center_code.w != 7)
                continue;
            const auto point = [&](math::Vec3 p) {
                queries.emplace_back(world_point(p, n), static_cast<float>(id));
            };
            const float b = n.parameters.x, t = n.parameters.y, h = 2 * n.parameters.z;
            for (unsigned i = 0; i < 24; ++i)
                point({(unit(rng) - .5F) * 1.2F, (unit(rng) - .5F) * (h + 2 * std::max(b, t)),
                       (unit(rng) - .5F) * 1.2F});
            const auto c = prepared.entries[id];
            if (c.w == 1) {
                for (float along : {-1e-7F, 0.F, 1e-7F, c.z * .5F, c.z - 1e-7F, c.z, c.z + 1e-7F})
                    for (float theta : {0.F, 1.F, 2.F, 3.F}) {
                        const float radial = b * c.y + along * c.x;
                        const float y = -b * c.x + along * c.y - n.parameters.z;
                        point({radial * std::cos(theta), y, radial * std::sin(theta)});
                    }
            }
        }
        host::CommonRunConfig config;
        config.headless = config.headless_render_only = config.require_validation = true;
        config.width = config.height = 16;
        config.frames = 1;
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [&](host::HeadlessPngContext& context) {
            auto& device = context.device();
            std::vector<vulkan::DescriptorSetBindingConfig> bindings;
            for (unsigned i : {2U, 3U, 15U, 19U})
                bindings.push_back(
                    {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
            vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
            const std::array layouts{descriptors.layout()};
            const std::array<VkPushConstantRange, 1> push{{{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4}}};
            render::ComputePipelineResource pipeline(
                device, {.shader_stage = render::compute_shader_file(
                             std::filesystem::path(argv[1]) / "prepared_cone_probe.comp.spv"),
                         .descriptor_set_layouts = layouts,
                         .push_constants = push});
            const auto make = [&](std::size_t size) {
                return std::make_unique<vulkan::Buffer>(
                    device, vulkan::BufferConfig{.size = size,
                                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 .memory_properties =
                                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
            };
            auto program = authored_program_bytes(&scene);
            auto program_buffer = make(program.size()),
                 query_buffer = make(queries.size() * sizeof(math::Vec4)),
                 coefficients = make(prepared.entries.size() * sizeof(math::Vec4));
            struct Result {
                math::Vec4 distance, normal_reference, normal_prepared;
            };
            std::vector<Result> rows(queries.size());
            auto output = make(rows.size() * sizeof(Result));
            program_buffer->upload(program.data(), program.size());
            query_buffer->upload(queries.data(), query_buffer->size());
            coefficients->upload(prepared.entries.data(), coefficients->size());
            vulkan::DescriptorWriteBatch writes;
            for (const auto& [binding, buffer] :
                 std::array<std::pair<unsigned, vulkan::Buffer*>, 4>{{{2, query_buffer.get()},
                                                                      {3, program_buffer.get()},
                                                                      {15, output.get()},
                                                                      {19, coefficients.get()}}})
                writes.storage_buffer(descriptors.set(), binding, buffer->handle(), buffer->size());
            writes.update(device);
            static_cast<void>(context.gpu().submit_and_wait(
                {.label = "prepared cone numerical gate",
                 .work = [&](vulkan::GpuOwnerContext& owner) {
                     vulkan::ImmediateCommands commands(owner);
                     const vulkan::CommandRecorder r(commands.command_buffer());
                     r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                     r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                           descriptors.set());
                     r.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                      static_cast<unsigned>(queries.size()));
                     r.dispatch(static_cast<unsigned>((queries.size() + 63) / 64), 1, 1);
                     const std::array<VkMemoryBarrier, 1> barrier{
                         {{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_HOST_READ_BIT}}};
                     r.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        VK_PIPELINE_STAGE_HOST_BIT, 0, barrier);
                     commands.submit_and_wait();
                 }}));
            output->download(rows.data(), output->size());
            Json receipt = {
                {"scope",
                 "Independent primitive numerical gate; no scene publication or renderer adoption"},
                {"queries", rows.size()},
                {"guardian_program_count", guardian_count},
                {"synthetic_primitive_count", scene.program.size() - guardian_count},
                {"table_bytes", coefficients->size()},
                {"cache_identity_checks_passed", true},
                {"groups", Json::object()},
                {"witnesses", Json::array()}};
            for (const auto group : {"guardian", "synthetic"})
                receipt["groups"][group] = {{"points", 0},
                                            {"changed", 0},
                                            {"point_failures", 0},
                                            {"normal_failures", 0},
                                            {"nonfinite", 0},
                                            {"max_gpu_difference", 0.0},
                                            {"max_normal_degrees", 0.0},
                                            {"max_reference_cpu_error", 0.0},
                                            {"max_prepared_cpu_error", 0.0}};
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const auto id = static_cast<unsigned>(queries[i].w);
                const auto& n = scene.program[id];
                const auto& row = rows[i];
                auto& g = receipt["groups"][id < guardian_count ? "guardian" : "synthetic"];
                g["points"] = g["points"].get<unsigned>() + 1;
                const float cpu = authored_primitive_distance(math::Vec3(queries[i]), n);
                const double delta = std::abs(static_cast<double>(row.distance.y) - row.distance.x);
                const double reference_error = std::abs(static_cast<double>(row.distance.x) - cpu);
                const double prepared_error = std::abs(static_cast<double>(row.distance.y) - cpu);
                double angle = 0;
                const glm::dvec3 a(row.normal_reference), b(row.normal_prepared);
                if (glm::length(a) > 1e-15 && glm::length(b) > 1e-15)
                    angle = std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)),
                                                 -1.0, 1.0)) *
                            180 / 3.141592653589793;
                const bool point_failure = prepared_error > 2e-6 && reference_error <= 2e-6;
                const bool normal_failure = angle > 1;
                for (const auto& [key, value] : std::array<std::pair<const char*, double>, 4>{
                         {{"max_gpu_difference", delta},
                          {"max_normal_degrees", angle},
                          {"max_reference_cpu_error", reference_error},
                          {"max_prepared_cpu_error", prepared_error}}})
                    g[key] = std::max(g[key].get<double>(), value);
                for (const auto& [key, flag] : std::array<std::pair<const char*, bool>, 4>{
                         {{"changed", row.distance.x != row.distance.y},
                          {"point_failures", point_failure},
                          {"normal_failures", normal_failure},
                          {"nonfinite", !std::isfinite(row.distance.y)}}})
                    if (flag)
                        g[key] = g[key].get<unsigned>() + 1;
                if ((point_failure || normal_failure || !std::isfinite(row.distance.y)) &&
                    receipt["witnesses"].size() < 20)
                    receipt["witnesses"].push_back({{"query", i},
                                                    {"source_index", id},
                                                    {"position", array(math::Vec3(queries[i]))},
                                                    {"parameters", array(math::Vec3(n.parameters))},
                                                    {"reference", row.distance.x},
                                                    {"prepared", row.distance.y},
                                                    {"cpu", cpu},
                                                    {"normal_degrees", angle}});
            }
            receipt["numerical_gate_passed"] = receipt["witnesses"].empty();
            std::ofstream(argv[3]) << receipt.dump(2) << '\n';
            std::cout << receipt.dump() << '\n';
        };
        callbacks.record_frame = [](host::HeadlessPngContext&, const host::HeadlessCaptureFrame&,
                                    VkCommandBuffer command,
                                    const host::HeadlessRenderTarget& target) {
            render::record_render_target_pass(vulkan::CommandRecorder(command),
                                              render::render_target_view(target),
                                              {.color = render::color_clear_value(0, 0, 0, 1)},
                                              [](const vulkan::CommandRecorder&) {});
        };
        return host::HeadlessPngHost(
                   {.run_config = config,
                    .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
                   std::move(callbacks))
            .run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
