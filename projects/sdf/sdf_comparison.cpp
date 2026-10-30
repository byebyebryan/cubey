#include "sdf_comparison.h"
#include "sdf_authored.h"
#include "sdf_field.h"
#include "sdf_prepared_cones.h"
#include "sdf_regions.h"
#include "sdf_shader_selection.h"
#include <chrono>
#include <cstring>
#include <cubey/asset/file_digest.h>
#include <cubey/core/file_io.h>
#include <cubey/engine/project_gpu_services.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/vulkan/immediate_commands.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace cubey::projects::sdf {
namespace {
using Json = nlohmann::json;
struct Query {
    math::Vec4 origin, direction;
};
struct Result {
    math::Vec4 hit, normal;
};
math::Vec3 vec(const Json& a) {
    return {a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>()};
}
Json donor(vulkan::Device& device, ProjectGpuServices& gpu, const Json& manifest) {
    std::vector<vulkan::DescriptorSetBindingConfig> bindings;
    for (unsigned i = 0; i < 17; ++i)
        bindings.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
    vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
    const std::array layouts{descriptors.layout()};
    const std::array<VkPushConstantRange, 1> push{{{VK_SHADER_STAGE_COMPUTE_BIT, 0, 12}}};
    render::ComputePipelineResource pipeline(
        device, {.shader_stage = render::compute_shader_file(
                     std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "comparison_donor.comp.spv"),
                 .descriptor_set_layouts = layouts,
                 .push_constants = push});
    Json cases = Json::array();
    for (const auto& c : manifest.at("cases")) {
        const auto scene = parse_authored_scene(c.at("scene").dump());
        const auto n = scene.program.size();
        if (n == 0 || n > 32)
            throw std::invalid_argument(
                "isolated donor qualification is limited to 32 instructions");
        struct Node {
            int type, index;
        };
        struct Op {
            float k, s;
        };
        std::vector<Node> nodes;
        std::vector<Op> ops;
        std::vector<std::uint32_t> parents(n, 65535), active(n), stack;
        for (std::size_t i = 0; i < n; ++i) {
            const auto code = static_cast<unsigned>(scene.program[i].center_code.w);
            nodes.push_back({code < 10 ? 1 : 0, static_cast<int>(i)});
            ops.push_back({scene.program[i].settings.y, (code == 11 || code == 13) ? -1.F : 1.F});
            active[i] = static_cast<std::uint32_t>(i);
            if (code >= 10) {
                const auto b = stack.back();
                stack.pop_back();
                const auto a = stack.back();
                stack.pop_back();
                parents[a] = parents[b] = static_cast<std::uint32_t>(i);
                if (code == 11 || code == 13)
                    active[b] |= 32768;
            }
            stack.push_back(static_cast<std::uint32_t>(i));
        }
        const auto evaluate = [&](math::Vec3 p, std::span<const std::uint32_t> refs) {
            std::vector<float> values;
            for (auto ref : refs) {
                const auto id = ref & 32767U;
                if (id >= n)
                    throw std::runtime_error("donor reference exceeds source program");
                const auto& node = nodes[id];
                float d;
                if (node.type == 1)
                    d = authored_primitive_distance(p, scene.program[id]);
                else {
                    if (values.size() < 2)
                        throw std::runtime_error("donor compacted program underflow");
                    const float b = values.back();
                    values.pop_back();
                    const float a = values.back();
                    values.pop_back();
                    const auto op = ops[id];
                    const float gap = std::max(0.F, op.k - std::abs(a - b));
                    d = op.s *
                        (std::min(op.s * a, op.s * b) - (op.k > 0 ? gap * gap * .25F / op.k : 0));
                }
                values.push_back((ref & 32768U) ? -d : d);
            }
            if (values.size() != 1)
                throw std::runtime_error("donor compacted program stack invalid");
            return values.front();
        };
        auto program = authored_program_bytes(&scene);
        const float guard = authored_evaluation_guard(scene);
        std::memcpy(program.data() + 12, &guard, 4);
        const std::array<std::size_t, 17> sizes{
            n * 8,  n * 8, n * 4, program.size(), n * 4,      64 * n * 4, 64 * n * 4, 4, 64 * 4, 4,
            64 * 4, 4,     4,     64 * 4,         64 * n * 4, 4,          64 * n * 4};
        std::vector<std::unique_ptr<vulkan::Buffer>> buffers;
        for (auto size : sizes) {
            buffers.push_back(std::make_unique<vulkan::Buffer>(
                device,
                vulkan::BufferConfig{.size = size,
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                     .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT}));
            std::vector<std::uint8_t> zero(size);
            buffers.back()->upload(zero.data(), size);
        }
        buffers[0]->upload(nodes.data(), n * 8);
        buffers[1]->upload(ops.data(), n * 8);
        buffers[2]->upload(parents.data(), n * 4);
        buffers[3]->upload(program.data(), program.size());
        buffers[4]->upload(active.data(), n * 4);
        const auto count = static_cast<std::uint32_t>(n);
        buffers[9]->upload(&count, 4);
        vulkan::DescriptorWriteBatch writes;
        for (unsigned i = 0; i < 17; ++i)
            writes.storage_buffer(descriptors.set(), i, buffers[i]->handle(), buffers[i]->size());
        writes.update(device);
        static_cast<void>(gpu.submit_and_wait(
            {.label = "isolated Lipschitz Pruning core",
             .work = [&](vulkan::GpuOwnerContext& owner) {
                 vulkan::ImmediateCommands commands(owner);
                 const vulkan::CommandRecorder r(commands.command_buffer());
                 r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                 r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                       descriptors.set());
                 r.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                  std::array<int, 3>{static_cast<int>(n), 4, 1});
                 r.dispatch(1, 1, 1);
                 const std::array<VkMemoryBarrier, 1> visibility{
                     {{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                       VK_ACCESS_HOST_READ_BIT}}};
                 r.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    VK_PIPELINE_STAGE_HOST_BIT, 0, visibility);
                 commands.submit_and_wait();
             }}));
        std::array<unsigned, 64> counts{}, offsets{};
        std::array<float, 64> far{};
        std::vector<std::uint32_t> refs(64 * n);
        buffers[10]->download(counts.data(), sizeof(counts));
        buffers[8]->download(offsets.data(), sizeof(offsets));
        buffers[13]->download(far.data(), sizeof(far));
        buffers[6]->download(refs.data(), refs.size() * 4);
        float max_error = 0;
        unsigned exact_points = 0, far_points = 0, far_cells = 0;
        std::size_t references = 0;
        for (unsigned z = 0; z < 4; ++z)
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 4; ++x) {
                    const unsigned id = (x & 1U) | ((y & 1U) << 1) | ((z & 1U) << 2) |
                                        ((x & 2U) << 2) | ((y & 2U) << 3) | ((z & 2U) << 4);
                    if (counts[id] > n || offsets[id] + counts[id] > refs.size())
                        throw std::runtime_error("donor output capacity exceeded");
                    references += counts[id];
                    far_cells += counts[id] == 0;
                    const auto compact =
                        std::span<const std::uint32_t>(refs).subspan(offsets[id], counts[id]);
                    for (float dz : {0.F, .5F, 1.F})
                        for (float dy : {0.F, .5F, 1.F})
                            for (float dx : {0.F, .5F, 1.F}) {
                                const math::Vec3 p{(static_cast<float>(x) + dx) * .25F - .5F,
                                                   (static_cast<float>(y) + dy) * .25F - .5F,
                                                   (static_cast<float>(z) + dz) * .25F - .5F};
                                const float full = evaluate(p, active);
                                if (compact.empty()) {
                                    ++far_points;
                                    if (!std::isfinite(far[id]) || full * far[id] <= 0 ||
                                        std::abs(far[id]) > std::abs(full) + 2e-6F)
                                        throw std::runtime_error("donor far-field bound failed");
                                } else {
                                    ++exact_points;
                                    max_error =
                                        std::max(max_error, std::abs(evaluate(p, compact) - full));
                                }
                            }
                }
        if (max_error > 2e-6F)
            throw std::runtime_error("donor exact-region comparison failed");
        cases.push_back({{"name", c.at("name")},
                         {"source_instructions", n},
                         {"references", references},
                         {"far_cells", far_cells},
                         {"exact_points", exact_points},
                         {"far_points", far_points},
                         {"max_exact_error", max_error}});
    }
    return {{"format", "cubey.sdf.donor-qualification"},
            {"donor_revision", "b3c5fee4d951ad8942775206d111cd421d2ef31c"},
            {"grid", 4},
            {"scope",
             "Adapted original compute core, first refinement, 32-bit descriptor storage and "
             "Cubey leaf evaluator; far constants checked as bounds, not exact values."},
            {"cases", cases}};
}
void execute(vulkan::Device& device, ProjectGpuServices& gpu, const SdfConfig& config) {
    std::ifstream input(config.comparison_input);
    const auto manifest = Json::parse(input);
    if (manifest.at("format") != "cubey.sdf.tracing-study" || manifest.at("version") != 1)
        throw std::invalid_argument("unknown tracing study format");
    if (config.comparison_donor) {
        std::ofstream output(config.comparison_output);
        if (!output)
            throw std::runtime_error("cannot write donor receipt");
        output << donor(device, gpu, manifest).dump(2) << '\n';
        return;
    }
    Json receipt{{"format", "cubey.sdf.tracing-results"}, {"version", 1}, {"cases", Json::array()}};
    const bool workload = manifest.value("workload", false);
    const bool prepared_cones =
        config.source_optimization == "prepared-cones" || config.source_optimization == "combined";
    if (prepared_cones && workload)
        throw std::invalid_argument("prepared-cones queries do not support workload counters");
    const unsigned work_stride = config.acceleration_variant != "off" ? 64U : 52U;
    if (workload && (config.csg_evaluation != "pruned" || config.surface_trace == "sampled"))
        throw std::invalid_argument("workload replay requires pruned source tracing");
    std::vector<vulkan::DescriptorSetBindingConfig> bindings{
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}};
    std::unique_ptr<Field> field;
    if (workload)
        bindings.push_back({18, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
    if (prepared_cones)
        bindings.push_back({19, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
    const bool pruned = config.csg_evaluation == "pruned" || cached_source_normals(config);
    if (pruned) {
        if (config.surface_trace == "sampled" && !cached_source_normals(config))
            throw std::invalid_argument("sampled/pruned is not a comparison route");
        bindings.push_back({16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
        bindings.push_back({17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
    }
    if (config.surface_trace != "analytic") {
        bindings.push_back({0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
        bindings.push_back(
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT});
        field = std::make_unique<Field>(device, gpu, 64);
    }
    vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
    const std::array layouts{descriptors.layout()};
    const std::array<VkPushConstantRange, 1> push{
        {{VK_SHADER_STAGE_COMPUTE_BIT, 0, workload ? 32U : 8U}}};
    std::string shader =
        workload ? (config.surface_trace == "analytic" ? "workload_analytic.comp.spv"
                                                       : "workload_hybrid.comp.spv")
        : config.surface_trace == "analytic"
            ? (pruned ? "comparison_analytic_pruned.comp.spv" : "comparison_full.comp.spv")
        : config.surface_trace == "hybrid"
            ? (pruned ? "comparison_hybrid_pruned.comp.spv" : "comparison_hybrid.comp.spv")
            : "comparison_sampled.comp.spv";
    if (config.comparison_tight_root)
        shader.insert(0, "tight_");
    if (config.acceleration_variant != "off")
        shader.insert(0, config.acceleration_variant == "control" ? "accel_"
                         : config.acceleration_variant == "root-far"
                             ? "root_far_"
                             : config.acceleration_variant + "_");
    if (prepared_cones)
        shader.insert(0, "prepared_");
    if (config.query_stepping != "ordinary")
        shader.insert(0, query_step_prefix(config));
    if (config.cached_quality != "ordinary")
        shader.insert(0, cached_quality_prefix(config));
    const auto query_shader_bytes =
        read_binary_file(std::filesystem::path(CUBEY_SDF_SHADER_DIR) / shader);
    receipt["query_shader"] = shader;
    receipt["query_shader_sha256"] =
        asset::sha256_hex(std::as_bytes(std::span(query_shader_bytes)));
    receipt["prepared_cones"] = prepared_cones;
    receipt["source_optimization"] = config.source_optimization;
    render::ComputePipelineResource pipeline(
        device, {.shader_stage = render::compute_shader_file(
                     std::filesystem::path(CUBEY_SDF_SHADER_DIR) / shader),
                 .descriptor_set_layouts = layouts,
                 .push_constants = push});
    for (const auto& c : manifest.at("cases")) {
        const auto start = std::chrono::steady_clock::now();
        const auto scene = parse_authored_scene(c.at("scene").dump());
        std::unique_ptr<GpuRegionPrograms> regions;
        if (pruned)
            regions = std::make_unique<GpuRegionPrograms>(
                device, scene, config.region_axis, region_reference_limit, false,
                config.acceleration_variant != "off" || cached_source_normals(config) ? &gpu
                                                                                      : nullptr,
                cached_source_normals(config) ? "root-far" : config.acceleration_variant);
        if (field)
            field->build(scene);
        std::vector<Query> queries;
        for (const auto& q : c.at("queries")) {
            const bool point = q.at("kind") == "point";
            const auto origin = vec(q.at(point ? "position" : "origin"));
            queries.push_back(
                {{origin, point ? 1.F : 0.F}, {point ? math::Vec3{0} : vec(q.at("direction")), 0}});
        }
        const auto declared_queries = queries.size();
        // Derived equivalence probes supplement the independent known answers.
        // Fixed grid, every region seam +/- the normal stencil, and leaf centers.
        const auto point = [&](math::Vec3 p) { queries.push_back({{p, 1}, {0, 0, 0, 0}}); };
        if (!workload) {
            for (int z = 0; z <= 8; ++z)
                for (int y = 0; y <= 8; ++y)
                    for (int x = 0; x <= 8; ++x)
                        point(math::Vec3{x, y, z} / 8.F - .5F);
            constexpr float delta = 1.F / (256 * 7 * 16);
            for (unsigned i = 0; i <= config.region_axis; ++i)
                for (int a = 0; a < 3; ++a)
                    for (float offset : {-delta, 0.F, delta}) {
                        math::Vec3 p{0};
                        p[a] = static_cast<float>(i) / static_cast<float>(config.region_axis) -
                               .5F + offset;
                        point(p);
                    }
            for (const auto& n : scene.program)
                if (n.center_code.w < 10) {
                    const auto p = math::Vec3(n.center_code);
                    point(p);
                    for (int a = 0; a < 3; ++a) {
                        auto d = math::Vec3{0};
                        d[a] = delta;
                        point(p - d);
                        point(p + d);
                    }
                }
        }
        if (queries.empty() || queries.size() > 65536)
            throw std::invalid_argument("study query budget");
        auto program = authored_program_bytes(&scene);
        const float guard = authored_evaluation_guard(scene);
        std::memcpy(program.data() + 12, &guard, 4);
        auto program_buffer = gpu.upload_device_buffer<std::uint8_t>(
            program, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "study program");
        std::unique_ptr<vulkan::Buffer> cone_buffer;
        if (prepared_cones) {
            auto cones = prepare_cones(scene);
            if (cones.entries.empty())
                cones.entries.emplace_back(0);
            cone_buffer = std::make_unique<vulkan::Buffer>(gpu.upload_device_buffer<math::Vec4>(
                cones.entries, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "study prepared cones"));
        }
        auto query_buffer = gpu.upload_device_buffer<Query>(
            queries, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "study queries");
        vulkan::Buffer output(
            device, vulkan::device_local_buffer_config(queries.size() * sizeof(Result),
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        std::unique_ptr<vulkan::Buffer> work_output;
        if (workload)
            work_output = std::make_unique<vulkan::Buffer>(
                device, vulkan::device_local_buffer_config(
                            queries.size() * work_stride * sizeof(std::uint32_t),
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        vulkan::DescriptorWriteBatch writes;
        if (regions) {
            writes.storage_buffer(descriptors.set(), 16, regions->cells->handle(),
                                  regions->cells->size());
            writes.storage_buffer(descriptors.set(), 17, regions->references->handle(),
                                  regions->references->size());
        }
        if (field) {
            writes.storage_buffer(descriptors.set(), 0, field->nodes().handle(),
                                  field->nodes().size());
            writes.combined_image_sampler(descriptors.set(), 1, field->atlas().sampler().handle(),
                                          field->atlas().view(), VK_IMAGE_LAYOUT_GENERAL);
        }
        writes.storage_buffer(descriptors.set(), 2, query_buffer.handle(), query_buffer.size());
        writes.storage_buffer(descriptors.set(), 3, program_buffer.handle(), program_buffer.size());
        writes.storage_buffer(descriptors.set(), 15, output.handle(), output.size());
        if (cone_buffer)
            writes.storage_buffer(descriptors.set(), 19, cone_buffer->handle(),
                                  cone_buffer->size());
        if (work_output)
            writes.storage_buffer(descriptors.set(), 18, work_output->handle(),
                                  work_output->size());
        writes.update(device);
        static_cast<void>(gpu.submit_and_wait(
            {.label = "sdf study queries", .work = [&](vulkan::GpuOwnerContext& owner) {
                 vulkan::ImmediateCommands commands(owner);
                 const vulkan::CommandRecorder r(commands.command_buffer());
                 r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                 r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                       descriptors.set());
                 if (workload) {
                     struct WorkPush {
                         std::array<std::uint32_t, 4> settings;
                         math::Vec4 sunlight;
                     };
                     static_assert(sizeof(WorkPush) == 32);
                     r.push_constants(
                         pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         WorkPush{{static_cast<std::uint32_t>(queries.size()), 64, 0, 0},
                                  {glm::normalize(vec(manifest.at("sunlight"))), 0}});
                 } else
                     r.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                      std::array<std::uint32_t, 2>{
                                          static_cast<std::uint32_t>(queries.size()), 64});
                 r.dispatch(static_cast<std::uint32_t>((queries.size() + 63) / 64), 1, 1);
                 commands.submit_and_wait();
             }}));
        const auto bytes = gpu.readback_buffer(output.handle(), output.size(), "study results");
        Json workload_files;
        if (work_output) {
            const auto stats = gpu.readback_buffer(work_output->handle(), work_output->size(),
                                                   "workload counters");
            const auto prefix =
                config.comparison_output.parent_path() /
                (config.comparison_output.stem().string() + "." + c.at("name").get<std::string>());
            const auto rays_path = prefix.string() + ".rays.bin";
            const auto stats_path = prefix.string() + ".work.bin";
            std::ofstream rays_file(rays_path, std::ios::binary),
                stats_file(stats_path, std::ios::binary);
            if (!rays_file || !stats_file)
                throw std::runtime_error("cannot write workload payload");
            rays_file.write(reinterpret_cast<const char*>(bytes.data()),
                            static_cast<std::streamsize>(bytes.size()));
            stats_file.write(reinterpret_cast<const char*>(stats.data()),
                             static_cast<std::streamsize>(stats.size()));
            if (!rays_file || !stats_file)
                throw std::runtime_error("incomplete workload payload");
            workload_files = {{"rays_file", rays_path},
                              {"stats_file", stats_path},
                              {"query_count", queries.size()},
                              {"rays_stride", sizeof(Result)},
                              {"stats_stride", work_stride * sizeof(std::uint32_t)},
                              {"layout",
                               "little-endian u32: totals[3][4], bins_low[3][4], bins_high[3][4], "
                               "walk[3][4], hit[4]; phases primary, normal, shadow"}};
        }
        Json results = Json::array();
        for (std::size_t i = 0; !workload && i < declared_queries; ++i) {
            Result r;
            std::memcpy(&r, bytes.data() + i * sizeof(Result), sizeof(Result));
            results.push_back(queries[i].origin.w > .5F
                                  ? Json{{"distance", r.hit.x}}
                                  : Json{{"t", r.hit.x},
                                         {"status", static_cast<int>(r.hit.y)},
                                         {"steps", r.hit.z},
                                         {"residual", r.hit.w},
                                         {"normal", {r.normal.x, r.normal.y, r.normal.z}}});
        }
        float probe_error = 0;
        for (std::size_t i = declared_queries; i < queries.size(); ++i) {
            Result r;
            std::memcpy(&r, bytes.data() + i * sizeof(Result), sizeof(Result));
            const float expected = authored_distance(math::Vec3(queries[i].origin), scene);
            const float error = std::abs(r.hit.x - expected);
            probe_error = std::max(probe_error, error);
            if (!std::isfinite(error) || error > 2e-6F)
                throw std::runtime_error("source/region GPU point equivalence failed: " +
                                         c.at("name").get<std::string>());
        }
        receipt["cases"].push_back(
            {{"name", c.at("name")},
             {"mode", config.surface_trace == "sampled"
                          ? "sampled"
                          : config.surface_trace + (pruned ? "-pruned" : "-full")},
             {"results", results},
             {"derived_probe_count", queries.size() - declared_queries},
             {"derived_probe_max_error", probe_error},
             {"instructions", scene.program.size()},
             {"geometry_hash", authored_geometry_hash(scene)},
             {"field_bytes", field ? field->stats().allocation_bytes : 0},
             {"program_bytes", program.size()},
             {"region_axis", regions ? regions->axis : 0},
             {"region_bytes", regions ? regions->allocation_bytes : 0},
             {"region_references", regions ? regions->reference_count : 0},
             {"region_fallback_cells", regions ? regions->fallback_cells : 0},
             {"region_far_cells", regions ? regions->far_cells : 0},
             {"acceleration_variant", config.acceleration_variant},
             {"region_build_upload_ms", regions ? regions->build_upload_ms : 0},
             {"field_build_ms", field ? field->stats().build_ms : 0},
             {"compile_upload_query_readback_ms",
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                  .count()}});
        if (workload)
            receipt["cases"].back()["workload"] = workload_files;
    }
    std::ofstream output(config.comparison_output);
    if (!output)
        throw std::runtime_error("cannot write tracing study receipt");
    output << receipt.dump(2) << '\n';
    std::cout << "sdf: study fixtures=" << receipt["cases"].size()
              << " receipt=" << config.comparison_output << '\n';
}
} // namespace
int run_tracing_comparison(const SdfConfig& config) {
    if (config.frame_profile != "off")
        throw std::invalid_argument(
            "frame profiles are native rendering diagnostics, not query routes");
    if ((config.acceleration_variant == "unclamped" && config.surface_trace != "hybrid") ||
        (config.acceleration_variant != "off" && config.acceleration_variant != "control" &&
         config.acceleration_variant != "unclamped" && config.surface_trace != "analytic"))
        throw std::invalid_argument("acceleration variant does not match its source mode");
    if (config.acceleration_variant != "off" &&
        (config.csg_evaluation != "pruned" || config.region_axis != 64 ||
         config.surface_trace == "sampled" || config.comparison_tight_root ||
         config.comparison_donor))
        throw std::invalid_argument("acceleration queries require pruned source at axis 64");
    if (!config.common.headless || config.comparison_output.empty())
        throw std::invalid_argument("tracing study requires --headless and --comparison-output");
    if (config.comparison_stage != "off" || config.comparison_residency != "host" ||
        (config.comparison_tight_root && config.csg_evaluation != "pruned"))
        throw std::invalid_argument(
            "query studies use no native comparison stage; tight root requires pruned evaluation");
    if (config.csg_evaluation == "pruned" &&
        (config.surface_trace == "sampled" ||
         (config.region_axis != 4 && config.region_axis != 16 && config.region_axis != 64)))
        throw std::invalid_argument(
            "pruned study requires source tracing and region axis 4, 16 or 64");
    UploadQueue uploads;
    std::optional<ProjectGpuServices> services;
    host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
        services.emplace(ctx.gpu(), uploads);
        execute(ctx.device(), *services, config);
    };
    callbacks.record_frame = [](host::HeadlessPngContext&, const host::HeadlessCaptureFrame&,
                                VkCommandBuffer command, const host::HeadlessRenderTarget& target) {
        render::record_render_target_pass(vulkan::CommandRecorder(command),
                                          render::render_target_view(target),
                                          {.color = render::color_clear_value(0, 0, 0, 1)},
                                          [](const vulkan::CommandRecorder&) {});
    };
    callbacks.shutdown = [&](host::HeadlessPngContext&) { services.reset(); };
    return host::HeadlessPngHost(
               {.run_config = config.common,
                .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
               std::move(callbacks))
        .run();
}
} // namespace cubey::projects::sdf
