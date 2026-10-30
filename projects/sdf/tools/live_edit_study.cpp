#include "../sdf_config.h"
#include "../sdf_live_edit.h"
#include "../sdf_renderer.h"
#include <chrono>
#include <cstring>
#include <cubey/asset/file_digest.h>
#include <cubey/core/file_io.h>
#include <cubey/core/image_io.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace cubey;
using namespace cubey::projects::sdf;
namespace {
using Json = nlohmann::json;
struct Query {
    math::Vec4 origin, direction;
};
struct QueryResult {
    math::Vec4 hit, normal, values;
    std::array<std::uint32_t, 4> counts, instructions;
    math::Vec4 corners0, corners1, sample_info;
};
static_assert(sizeof(QueryResult) == 128);
std::optional<float> stored_base(math::Vec3 p, const QueryResult& r) {
    if (r.sample_info.w == 0.F)
        return std::nullopt;
    const auto cell =
        glm::clamp(glm::ivec3(glm::floor((p + .5F) * 256.F)), glm::ivec3{0}, glm::ivec3{255});
    const auto local =
        glm::clamp((p + .5F) * 256.F - math::Vec3{cell}, math::Vec3{0}, math::Vec3{1}) * 7.F;
    const auto f = local - math::Vec3{glm::min(glm::ivec3{glm::floor(local)}, glm::ivec3{6})};
    const auto a = r.corners0, b = r.corners1;
    return std::lerp(std::lerp(std::lerp(a.x, a.y, f.x), std::lerp(a.z, a.w, f.x), f.y),
                     std::lerp(std::lerp(b.x, b.y, f.x), std::lerp(b.z, b.w, f.x), f.y), f.z);
}
math::Vec3 vec(const Json& a) {
    const math::Vec3 value{a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>()};
    if (a.size() != 3 || !std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z))
        throw std::invalid_argument("nonfinite or malformed live query vector");
    return value;
}
std::vector<Query> parse_queries(const Json& input) {
    std::vector<Query> result;
    for (const auto& q : input.at("queries")) {
        const auto kind = q.at("kind").get<std::string>();
        if (kind != "point" && kind != "ray")
            throw std::invalid_argument("unknown live query kind");
        const bool point = kind == "point";
        const auto p = vec(q.at(point ? "position" : "origin"));
        const auto d = point ? math::Vec3{0} : vec(q.at("direction"));
        if (!point && std::abs(glm::length(d) - 1.F) > 1e-5F)
            throw std::invalid_argument("live rays require unit directions");
        result.push_back({{p, point ? 1.F : 0.F}, {d, 0}});
    }
    if (result.empty() || result.size() > 65536)
        throw std::invalid_argument("live query count must be 1..65536");
    return result;
}
void save(const std::filesystem::path& path, const Json& value) {
    std::ofstream file(path);
    file << value.dump(2) << '\n';
    if (!file)
        throw std::runtime_error("cannot write live study receipt");
}
Json identity(const LiveEditProgram& program) {
    return {{"base_geometry_hash", authored_geometry_hash(program.base)},
            {"edited_geometry_hash", authored_geometry_hash(program.edited)},
            {"base_instructions", program.base.program.size()},
            {"edited_instructions", program.edited.program.size()},
            {"base_leaves", program.base.leaf_count},
            {"edited_leaves", program.edited.leaf_count},
            {"pending_leaves", program.pending_leaves},
            {"suffix_instructions", program.controls.header[1]}};
}
std::unique_ptr<vulkan::Buffer> control_buffer(vulkan::Device& device,
                                               const LiveEditControls& controls) {
    auto buffer = std::make_unique<vulkan::Buffer>(
        device, vulkan::BufferConfig{.size = sizeof(controls),
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                     .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
    buffer->upload(&controls, sizeof(controls));
    return buffer;
}
Json run_queries(vulkan::Device& device, ProjectGpuServices& gpu, const Json& manifest,
                 const std::vector<LiveEditProgram>& programs) {
    const std::array bindings{
        vulkan::DescriptorSetBindingConfig{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_SHADER_STAGE_COMPUTE_BIT},
        vulkan::DescriptorSetBindingConfig{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                           VK_SHADER_STAGE_COMPUTE_BIT},
        vulkan::DescriptorSetBindingConfig{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_SHADER_STAGE_COMPUTE_BIT},
        vulkan::DescriptorSetBindingConfig{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_SHADER_STAGE_COMPUTE_BIT},
        vulkan::DescriptorSetBindingConfig{15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_SHADER_STAGE_COMPUTE_BIT},
        vulkan::DescriptorSetBindingConfig{16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_SHADER_STAGE_COMPUTE_BIT},
        vulkan::DescriptorSetBindingConfig{17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_SHADER_STAGE_COMPUTE_BIT},
        vulkan::DescriptorSetBindingConfig{21, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_SHADER_STAGE_COMPUTE_BIT}};
    vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
    const std::array layouts{descriptors.layout()};
    const std::array<VkPushConstantRange, 1> push{{{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8}}};
    const auto shader = std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "live_edit_queries.comp.spv";
    const auto shader_bytes = read_binary_file(shader);
    render::ComputePipelineResource pipeline(device,
                                             {.shader_stage = render::compute_shader_file(shader),
                                              .descriptor_set_layouts = layouts,
                                              .push_constants = push});
    Field field(device, gpu);
    Json receipt{{"format", "cubey.sdf.live-query-results"},
                 {"version", 1},
                 {"query_shader_sha256", asset::sha256_hex(std::as_bytes(std::span(shader_bytes)))},
                 {"cases", Json::array()}};
    for (std::size_t case_index = 0; case_index < programs.size(); ++case_index) {
        const auto& program = programs.at(case_index);
        const auto& input = manifest.at("cases").at(case_index);
        field.build(program.base);
        GpuRegionPrograms regions(device, program.base, 64, region_reference_limit, false, &gpu,
                                  "root-far");
        auto controls = control_buffer(device, program.controls);
        auto bytes = authored_program_bytes(&program.edited);
        const float guard = authored_evaluation_guard(program.edited);
        std::memcpy(bytes.data() + 12, &guard, 4);
        auto source = gpu.upload_device_buffer<std::uint8_t>(
            bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "live combined program");
        const auto queries = parse_queries(input);
        auto query_buffer = gpu.upload_device_buffer<Query>(
            queries, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "live queries");
        vulkan::Buffer output(
            device, vulkan::device_local_buffer_config(queries.size() * sizeof(QueryResult),
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        vulkan::DescriptorWriteBatch writes;
        const std::array<std::pair<std::uint32_t, const vulkan::Buffer*>, 7> buffers{
            {{0U, &field.nodes()},
             {2U, &query_buffer},
             {3U, &source},
             {15U, &output},
             {16U, regions.cells.get()},
             {17U, regions.references.get()},
             {21U, controls.get()}}};
        for (const auto& [binding, buffer] : buffers)
            writes.storage_buffer(descriptors.set(), binding, buffer->handle(), buffer->size());
        writes.combined_image_sampler(descriptors.set(), 1, field.atlas().sampler().handle(),
                                      field.atlas().view(), VK_IMAGE_LAYOUT_GENERAL);
        writes.update(device);
        static_cast<void>(gpu.submit_and_wait(
            {.label = "live frozen-base queries", .work = [&](vulkan::GpuOwnerContext& owner) {
                 vulkan::ImmediateCommands commands(owner);
                 const vulkan::CommandRecorder r(commands.command_buffer());
                 r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                 r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                       descriptors.set());
                 r.push_constants(
                     pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     std::array<std::uint32_t, 2>{static_cast<std::uint32_t>(queries.size()), 64});
                 r.dispatch(static_cast<std::uint32_t>((queries.size() + 63) / 64), 1, 1);
                 commands.submit_and_wait();
             }}));
        const auto result_bytes =
            gpu.readback_buffer(output.handle(), output.size(), "live query results");
        Json rows = Json::array();
        float max_point_error = 0;
        for (std::size_t i = 0; i < queries.size(); ++i) {
            QueryResult result;
            std::memcpy(&result, result_bytes.data() + i * sizeof(result), sizeof(result));
            Json row{{"counts", result.counts}, {"instructions", result.instructions}};
            if (queries[i].origin.w > .5F) {
                const float expected = live_edit_cpu_distance(
                    math::Vec3(queries[i].origin), program, program.controls.header[2] == 1,
                    stored_base(math::Vec3(queries[i].origin), result));
                const float error = std::abs(expected - result.hit.x);
                max_point_error = std::max(max_point_error, error);
                row.update(
                    {{"distance", result.hit.x},
                     {"cpu_live_distance", expected},
                     {"cpu_point_error", error},
                     {"gpu_full_source", result.values.x},
                     {"interval", {result.values.y, result.values.z}},
                     {"gpu_base_raw", result.values.w},
                     {"stored_corners",
                      {result.corners0.x, result.corners0.y, result.corners0.z, result.corners0.w,
                       result.corners1.x, result.corners1.y, result.corners1.z, result.corners1.w}},
                     {"stored_sample", result.sample_info.w != 0.F}});
            } else {
                const auto position =
                    math::Vec3(queries[i].origin) + result.hit.x * math::Vec3(queries[i].direction);
                row.update(
                    {{"t", result.hit.x},
                     {"status", static_cast<int>(result.hit.y)},
                     {"steps", result.hit.z},
                     {"residual", result.hit.w},
                     {"normal", {result.normal.x, result.normal.y, result.normal.z}},
                     {"gpu_source_residual", result.values.x},
                     {"cpu_live_residual",
                      result.hit.y == 1.F ? live_edit_cpu_distance(position, program,
                                                                   program.controls.header[2] == 1,
                                                                   stored_base(position, result))
                                          : 0.F}});
            }
            rows.push_back(std::move(row));
        }
        auto row = identity(program);
        row.update({{"name", input.at("name")},
                    {"mode", program.controls.header[2] == 1 ? "live-source" : "live-cached"},
                    {"results", rows},
                    {"cpu_point_max_error", max_point_error},
                    {"field_bytes", field.stats().allocation_bytes},
                    {"field_builds", field.stats().build_count},
                    {"field_build_ms", field.stats().build_ms},
                    {"region_bytes", regions.allocation_bytes},
                    {"region_ms", regions.build_upload_ms}});
        receipt["cases"].push_back(std::move(row));
    }
    return receipt;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path input_path, output_path;
        bool queries = false, validate_only = false, source_defined = false;
        std::vector<char*> arguments{argv[0]};
        for (int i = 1; i < argc; ++i) {
            const std::string_view flag(argv[i]);
            if (flag == "--live-input" || flag == "--live-output" || flag == "--live-policy") {
                if (++i >= argc)
                    throw std::invalid_argument("missing private live option value");
                if (flag == "--live-input")
                    input_path = argv[i];
                else if (flag == "--live-output")
                    output_path = argv[i];
                else {
                    const std::string_view policy(argv[i]);
                    if (policy != "source" && policy != "cached")
                        throw std::invalid_argument("live policy must be cached or source");
                    source_defined = policy == "source";
                }
            } else if (flag == "--live-queries")
                queries = true;
            else if (flag == "--live-validate-only")
                validate_only = true;
            else
                arguments.push_back(argv[i]);
        }
        config::ParseResult parsed;
        auto config =
            parse_sdf_config(static_cast<int>(arguments.size()), arguments.data(), &parsed);
        const auto& override = config.route_overrides;
        if (parsed.path_was_assigned("render-mode") || override.surface_trace ||
            override.csg_evaluation || override.region_axis || override.acceleration_variant ||
            override.source_shaders || override.source_optimization || override.frame_profile ||
            override.cached_quality || override.query_stepping)
            throw std::invalid_argument("private live policy cannot be combined with public "
                                        "rendering modes or low-level route overrides");
        if (input_path.empty() || output_path.empty() || !config.common.headless ||
            config.timeline.motion != "static" || config.lighting != "atmosphere" ||
            config.lighting_cycle != "frozen" || config.debug || config.ao || config.ground ||
            config.clouds || config.occluder || config.supersample != 1 ||
            config.atlas_axis != 64 || !config.comparison_input.empty() ||
            !config.comparison_actions.empty() || !config.query_workload_output.empty() ||
            !config.workshop.empty() || config.frame_profile != "off" ||
            config.comparison_stage != "off" || config.cached_quality != "ordinary" ||
            config.query_stepping != "ordinary")
            throw std::invalid_argument(
                "private live study requires explicit input/output, static headless opaque "
                "atmosphere SS1 and no other studies/overlays");
        std::ifstream input(input_path);
        const auto manifest = Json::parse(input);
        if (manifest.at("format") != "cubey.sdf.live-edit-study" || manifest.at("version") != 1 ||
            manifest.at("cases").empty() || manifest.at("cases").size() > 128)
            throw std::invalid_argument("invalid live study manifest");
        std::vector<LiveEditProgram> programs;
        for (const auto& c : manifest.at("cases")) {
            programs.push_back(prepare_live_edit(parse_authored_scene(c.at("base").dump()),
                                                 parse_authored_scene(c.at("scene").dump()),
                                                 source_defined));
            if (queries)
                static_cast<void>(parse_queries(c));
        }
        if (!queries && programs.size() != 1)
            throw std::invalid_argument("native live captures require exactly one frozen base");
        std::vector<LiveEditProgram> states;
        if (manifest.contains("states")) {
            if (queries || manifest.at("states").size() != config.common.frames ||
                config.common.frames > 128 || !config.common.headless_render_only ||
                !config.common.profile_output_prefix.empty() || config.common.print_frame_stats)
                throw std::invalid_argument("live pose states require matched bounded render-only "
                                            "frames without profiling");
            for (const auto& state : manifest.at("states"))
                states.push_back(prepare_live_edit(programs[0].base,
                                                   parse_authored_scene(state.at("scene").dump()),
                                                   source_defined));
        }
        if (!config.quality_output.empty() &&
            (queries || config.common.frames != 1 || !config.common.profile_output_prefix.empty() ||
             16ULL + static_cast<std::uint64_t>(config.common.width) * config.common.height *
                         16ULL >
                 32ULL * 1024 * 1024))
            throw std::invalid_argument(
                "live quality capture requires one unprofiled frame and at most 32 MiB");
        config.named_render_mode = false;
        config.surface_trace = "analytic";
        config.csg_evaluation = "pruned";
        config.region_axis = 64;
        config.acceleration_variant = "root-far";
        config.source_shaders = "automatic";
        config.source_optimization = "automatic";
        Json receipt{
            {"format", "cubey.sdf.live-edit-results"},
            {"version", 1},
            {"policy", source_defined ? "source" : "cached"},
            {"scope", "Private frozen-base study; no background baking, continuous input or VR"},
            {"programs", Json::array()}};
        for (const auto& p : programs)
            receipt["programs"].push_back(identity(p));
        if (validate_only) {
            receipt["validated_without_gpu"] = true;
            save(output_path, receipt);
            return 0;
        }
        ProjectRuntimeAdapter runtime;
        std::unique_ptr<Field> field;
        std::unique_ptr<GpuRegionPrograms> regions;
        std::unique_ptr<vulkan::Buffer> controls;
        Renderer renderer;
        std::optional<ProjectGpuReadbackTicket> readback;
        Json pose_rows = Json::array();
        float yaw = config.yaw, pitch = config.pitch, distance = config.distance;
        auto finish_pose = [&] {
            if (!readback)
                return;
            runtime.gpu().wait_queue_idle("live pose completed frame");
            auto pixels = runtime.gpu().take_completed_readback(*readback);
            auto path = output_path;
            path.replace_extension(
                "frame-" + std::to_string(pose_rows.back().at("frame").get<unsigned>()) + ".png");
            write_png_rgba8(path, pixels.width, pixels.height, pixels.rgba8);
            pose_rows.back()["frame_png"] = path.string();
            readback.reset();
        };
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
            runtime.attach_gpu_if_needed(ctx.gpu());
            if (queries) {
                receipt["queries"] = run_queries(ctx.device(), runtime.gpu(), manifest, programs);
                return;
            }
            const auto& initial = states.empty() ? programs[0] : states[0];
            field = std::make_unique<Field>(ctx.device(), runtime.gpu());
            field->build(initial.base);
            regions = std::make_unique<GpuRegionPrograms>(ctx.device(), initial.base, 64,
                                                          region_reference_limit, false,
                                                          &runtime.gpu(), "root-far");
            controls = control_buffer(ctx.device(), initial.controls);
            renderer.create(ctx.device(), ctx.gpu(), ctx.render_target().extent,
                            ctx.render_target().format,
                            host::headless_capture_frame_slot_count(config.common), field.get(),
                            config, &initial.edited, regions.get(), {}, controls.get());
            receipt["field_bytes"] = field->stats().allocation_bytes;
            receipt["field_build_ms"] = field->stats().build_ms;
            receipt["region_bytes"] = regions->allocation_bytes;
            receipt["region_ms"] = regions->build_upload_ms;
        };
        callbacks.before_frame = [&](host::HeadlessPngContext& ctx,
                                     const host::HeadlessCaptureFrame& frame) {
            if (states.empty())
                return;
            finish_pose();
            const auto start = std::chrono::steady_clock::now();
            const auto& state = states.at(frame.index);
            if (frame.index != 0) {
                runtime.gpu().wait_queue_idle("live pose program replacement");
                controls->upload(&state.controls, sizeof(state.controls));
                renderer.update_authored(ctx.device(), &state.edited, regions.get());
            }
            const auto& pose = manifest.at("states").at(frame.index);
            yaw = pose.value("yaw", config.yaw);
            pitch = pose.value("pitch", config.pitch);
            distance = pose.value("distance", config.distance);
            auto row = identity(state);
            row.update(
                {{"frame", frame.index},
                 {"field_builds", field->stats().build_count},
                 {"region_builds", 1},
                 {"yaw", yaw},
                 {"pitch", pitch},
                 {"distance", distance},
                 {"serialized_upload_prepare_ms", std::chrono::duration<double, std::milli>(
                                                      std::chrono::steady_clock::now() - start)
                                                      .count()}});
            pose_rows.push_back(std::move(row));
        };
        callbacks.record_frame = [&](host::HeadlessPngContext& ctx,
                                     const host::HeadlessCaptureFrame& frame,
                                     VkCommandBuffer command,
                                     const host::HeadlessRenderTarget& target) {
            if (queries) {
                render::record_render_target_pass(vulkan::CommandRecorder(command),
                                                  render::render_target_view(target),
                                                  {.color = render::color_clear_value(0, 0, 0, 1)},
                                                  [](const vulkan::CommandRecorder&) {});
                return;
            }
            renderer.record(ctx.device(), command, target, frame.frame_slot, false, config, yaw,
                            pitch, distance, ctx.profile_recorder(), frame.index);
            if (!states.empty()) {
                vulkan::transition_image_layout(
                    command, vulkan::finish_color_attachment_for_readback_transition(target.image));
                readback = runtime.gpu().enqueue_rgba8_image_readback(target.image, target.extent,
                                                                      "live pose pixels");
            }
        };
        callbacks.shutdown = [&](host::HeadlessPngContext&) {
            if (!queries) {
                finish_pose();
                receipt["field_builds"] = field->stats().build_count;
                receipt["region_builds"] = 1;
                receipt["poses"] = pose_rows;
                renderer.destroy();
            }
            field.reset();
            regions.reset();
            controls.reset();
            runtime.detach_gpu_if_attached();
            save(output_path, receipt);
        };
        if (queries) {
            config.common.headless_render_only = true;
            config.common.frames = 1;
            config.common.width = config.common.height = 16;
        }
        return host::HeadlessPngHost(
                   {.run_config = config.common,
                    .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
                   std::move(callbacks))
            .run();
    } catch (const std::exception& error) {
        std::cerr << "live study: " << error.what() << '\n';
        return 1;
    }
}
