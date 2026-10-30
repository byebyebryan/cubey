#include "../sdf_cached_commit.h"
#include "../sdf_cached_edit.h"
#include "../sdf_config.h"
#include "../sdf_render_modes.h"
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
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void save(const std::filesystem::path& path, const Json& value) {
    std::ofstream file(path);
    file << value.dump(2) << '\n';
    if (!file)
        throw std::runtime_error("cannot save cached edit receipt");
}
Json identity(const CachedEditProgram& p) {
    return {{"base_geometry_hash", authored_geometry_hash(p.base)},
            {"edited_geometry_hash", authored_geometry_hash(p.edited)},
            {"base_leaves", p.base.leaf_count},
            {"edited_leaves", p.edited.leaf_count},
            {"base_instructions", p.base.program.size()},
            {"edited_instructions", p.edited.program.size()},
            {"pending_operations", p.controls.header[0]},
            {"subtract", p.controls.header[1] != 0},
            {"retained_history_steps", p.edited.history.size()}};
}
struct Query {
    math::Vec4 origin, direction;
};
struct Result {
    math::Vec4 hit, normal, diagnostic;
};
static_assert(sizeof(Result) == 48);
math::Vec3 vec(const Json& v) {
    if (!v.is_array() || v.size() != 3)
        throw std::invalid_argument("invalid edit query vector");
    math::Vec3 p{v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw std::invalid_argument("nonfinite edit query");
    return p;
}
std::vector<Query> queries(const Json& c) {
    std::vector<Query> result;
    for (const auto& q : c.at("queries")) {
        if (q.at("kind") != "ray")
            throw std::invalid_argument("cached edit queries require rays");
        const auto origin = vec(q.at("origin")), direction = vec(q.at("direction"));
        if (std::abs(glm::length(direction) - 1.F) > 1e-5F)
            throw std::invalid_argument("cached edit rays require unit directions");
        result.push_back({{origin, 0}, {direction, 0}});
    }
    if (result.empty() || result.size() > 65536)
        throw std::invalid_argument("edit query count must be 1..65536");
    return result;
}
std::unique_ptr<vulkan::Buffer> controls(vulkan::Device& device, const CachedEditControls& data) {
    auto p = std::make_unique<vulkan::Buffer>(
        device, vulkan::BufferConfig{.size = sizeof(data),
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                     .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
    p->upload(&data, sizeof(data));
    return p;
}
Json run_queries(vulkan::Device& device, ProjectGpuServices& gpu, const Json& input,
                 const std::vector<CachedEditProgram>& programs) {
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
        vulkan::DescriptorSetBindingConfig{21, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           VK_SHADER_STAGE_COMPUTE_BIT}};
    vulkan::DescriptorSetBundle descriptor(device, vulkan::DescriptorSetInfo(bindings));
    const std::array layouts{descriptor.layout()};
    const std::array<VkPushConstantRange, 1> push{{{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8}}};
    const auto path = std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "cached_edit_queries.comp.spv";
    render::ComputePipelineResource pipeline(device,
                                             {.shader_stage = render::compute_shader_file(path),
                                              .descriptor_set_layouts = layouts,
                                              .push_constants = push});
    const auto shader_bytes = read_binary_file(path);
    Json result{{"shader_sha256", asset::sha256_hex(std::as_bytes(std::span(shader_bytes)))},
                {"cases", Json::array()}};
    Field field(device, gpu);
    for (std::size_t i = 0; i < programs.size(); ++i) {
        const auto& p = programs.at(i);
        field.build(p.base);
        const auto qs = queries(input.at("cases").at(i));
        auto data = gpu.upload_device_buffer<Query>(qs, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                    "one edit rays");
        auto bytes = authored_program_bytes(&p.edited);
        auto source = gpu.upload_device_buffer<std::uint8_t>(
            bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "retained edited source");
        auto edit = controls(device, p.controls);
        vulkan::Buffer output(
            device, vulkan::device_local_buffer_config(qs.size() * sizeof(Result),
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        vulkan::DescriptorWriteBatch writes;
        const std::array<std::pair<unsigned, const vulkan::Buffer*>, 5> buffers{
            {{0, &field.nodes()}, {2, &data}, {3, &source}, {15, &output}, {21, edit.get()}}};
        for (const auto& [binding, b] : buffers)
            writes.storage_buffer(descriptor.set(), binding, b->handle(), b->size());
        writes.combined_image_sampler(descriptor.set(), 1, field.atlas().sampler().handle(),
                                      field.atlas().view(), VK_IMAGE_LAYOUT_GENERAL);
        writes.update(device);
        static_cast<void>(gpu.submit_and_wait(
            {.label = "one edit query batch", .work = [&](vulkan::GpuOwnerContext& owner) {
                 vulkan::ImmediateCommands commands(owner);
                 vulkan::CommandRecorder r(commands.command_buffer());
                 r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                 r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                       descriptor.set());
                 r.push_constants(
                     pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     std::array<std::uint32_t, 2>{static_cast<std::uint32_t>(qs.size()), 64});
                 r.dispatch(static_cast<std::uint32_t>((qs.size() + 63) / 64), 1, 1);
                 commands.submit_and_wait();
             }}));
        const auto raw = gpu.readback_buffer(output.handle(), output.size(), "one edit results");
        Json rows = Json::array();
        for (std::size_t j = 0; j < qs.size(); ++j) {
            Result r;
            std::memcpy(&r, raw.data() + j * sizeof(Result), sizeof(r));
            rows.push_back({{"t", r.hit.x},
                            {"status", static_cast<int>(r.hit.y)},
                            {"steps", r.hit.z},
                            {"pending_surface", r.hit.w != 0.F},
                            {"normal", {r.normal.x, r.normal.y, r.normal.z}},
                            {"source_residual", r.diagnostic.x}});
        }
        auto row = identity(p);
        row.update({{"name", input.at("cases").at(i).at("name")}, {"results", rows}});
        result["cases"].push_back(std::move(row));
    }
    return result;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path input_path, output_path;
        std::string phase = "preview";
        std::string builder_name = "full";
        bool query = false, validate = false;
        std::vector<char*> arguments{argv[0]};
        for (int i = 1; i < argc; ++i) {
            const std::string_view flag(argv[i]);
            if (flag == "--edit-input" || flag == "--edit-output" || flag == "--edit-phase" ||
                flag == "--edit-commit-builder") {
                if (++i >= argc)
                    throw std::invalid_argument("missing edit study value");
                if (flag == "--edit-input")
                    input_path = argv[i];
                else if (flag == "--edit-output")
                    output_path = argv[i];
                else if (flag == "--edit-phase")
                    phase = argv[i];
                else
                    builder_name = argv[i];
            } else if (flag == "--edit-queries")
                query = true;
            else if (flag == "--edit-validate-only")
                validate = true;
            else
                arguments.push_back(argv[i]);
        }
        config::ParseResult parsed;
        auto config =
            parse_sdf_config(static_cast<int>(arguments.size()), arguments.data(), &parsed);
        const auto& overrides = config.route_overrides;
        if (input_path.empty() || output_path.empty() || !config.common.headless ||
            config.timeline.motion != "static" || config.lighting != "atmosphere" ||
            config.lighting_cycle != "frozen" || config.debug || config.ao || config.ground ||
            config.clouds || config.occluder || config.supersample != 1 ||
            config.atlas_axis != 64 || !config.comparison_input.empty() ||
            !config.comparison_actions.empty() || !config.query_workload_output.empty() ||
            !config.workshop.empty() || parsed.path_was_assigned("render-mode") ||
            overrides.surface_trace || overrides.csg_evaluation || overrides.cached_quality ||
            overrides.acceleration_variant || overrides.region_axis || overrides.source_shaders ||
            overrides.source_optimization || overrides.query_stepping || overrides.frame_profile)
            throw std::invalid_argument("one-edit study requires static opaque headless SS1 "
                                        "without other studies or rendering overrides");
        if (phase != "base" && phase != "preview" && phase != "committed" && phase != "sequence")
            throw std::invalid_argument("invalid edit phase");
        if (builder_name != "full" && builder_name != "reuse" && builder_name != "pruned")
            throw std::invalid_argument("invalid commit builder");
        if (builder_name != "full" &&
            (query || validate || (phase != "sequence" && phase != "committed")))
            throw std::invalid_argument("commit builder needs an actual commit phase");
        const auto builder_mode =
            builder_name == "reuse" ? CommitBuilder::Reuse : CommitBuilder::Pruned;
        if (query && phase != "preview")
            throw std::invalid_argument("edit query batches describe the preview phase only");
        const auto input = Json::parse(std::ifstream(input_path));
        if (input.at("format") != "cubey.sdf.cached-edit-study" || input.at("version") != 1 ||
            input.at("cases").empty() || input.at("cases").size() > 128)
            throw std::invalid_argument("invalid cached edit manifest");
        std::vector<CachedEditProgram> programs;
        for (const auto& c : input.at("cases")) {
            programs.push_back(prepare_cached_edit(parse_authored_scene(c.at("base").dump()),
                                                   parse_authored_scene(c.at("scene").dump())));
            if (query)
                static_cast<void>(queries(c));
        }
        if (!query && programs.size() != 1)
            throw std::invalid_argument("native edit capture requires one case");
        const bool sequence = phase == "sequence";
        const bool fault = input.value("fault_before_publish", false);
        const auto failure_kind =
            input.value("candidate_failure", std::string("discard-after-submit"));
        if (failure_kind != "allocation" && failure_kind != "cancel-before-submit" &&
            failure_kind != "stale" && failure_kind != "discard-after-submit")
            throw std::invalid_argument("invalid candidate failure disposition");
        const unsigned sequence_frames = fault ? 5U : 4U;
        if (sequence &&
            (query || config.common.frames != sequence_frames ||
             !config.common.headless_render_only || !config.common.profile_output_prefix.empty() ||
             config.common.print_frame_stats || !config.quality_output.empty() ||
             !programs[0].controls.header[0]))
            throw std::invalid_argument("edit sequence requires matched render-only capture frames "
                                        "and one pending operation");
        if (!config.quality_output.empty() &&
            (query || config.common.frames != 1 || !config.common.profile_output_prefix.empty() ||
             16ULL + static_cast<std::uint64_t>(config.common.width) * config.common.height *
                         16ULL >
                 32ULL * 1024 * 1024))
            throw std::invalid_argument("edit payload requires one unprofiled native frame");
        config = config_for_render_mode(config, "cached", true);
        Json receipt{{"format", "cubey.sdf.cached-edit-results"},
                     {"version", 1},
                     {"phase", phase},
                     {"scope", "Private one-operation Cached preview; serialized commit, no "
                               "continuous input/updater/VR"},
                     {"programs", Json::array()},
                     {"frames", Json::array()},
                     {"commits", Json::array()}};
        for (const auto& p : programs)
            receipt["programs"].push_back(identity(p));
        receipt["commit_builder"] = builder_name;
        if (validate) {
            receipt["validated_without_gpu"] = true;
            save(output_path, receipt);
            return 0;
        }
        ProjectRuntimeAdapter runtime;
        std::unique_ptr<Field> base_field, candidate_field;
        std::unique_ptr<CachedCommitStudy> committer;
        std::unique_ptr<vulkan::Buffer> edit;
        Renderer main_renderer, preview_renderer, candidate_renderer;
        bool main_created = false, preview_created = false, candidate_created = false,
             published = false;
        bool pending = !sequence && programs[0].controls.header[0] != 0;
        std::optional<ProjectGpuReadbackTicket> readback;
        std::optional<Clock::time_point> commit_started;
        auto finish_frame = [&] {
            if (!readback)
                return;
            runtime.gpu().wait_queue_idle("one edit completed frame");
            auto pixels = runtime.gpu().take_completed_readback(*readback);
            auto& row = receipt["frames"].back();
            if (row.at("display") == "committed" && pending) {
                pending = false;
                receipt["pending_retired_after_completed_frame"] = row.at("frame");
                receipt["commits"].back()["to_completed_replacement_frame_ms"] =
                    elapsed(*commit_started);
            }
            row["pending_after_completed_frame"] = pending ? 1 : 0;
            auto path = output_path;
            path.replace_extension("frame-" + std::to_string(row.at("frame").get<unsigned>()) +
                                   ".png");
            write_png_rgba8(path, pixels.width, pixels.height, pixels.rgba8);
            row["image"] = path.string();
            readback.reset();
        };
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
            runtime.attach_gpu_if_needed(ctx.gpu());
            if (query) {
                receipt["queries"] = run_queries(ctx.device(), runtime.gpu(), input, programs);
                return;
            }
            const auto& p = programs[0];
            base_field = std::make_unique<Field>(ctx.device(), runtime.gpu());
            const auto& initial =
                phase == "committed" && builder_name == "full" ? p.edited : p.base;
            base_field->build(initial);
            if (builder_name != "full") {
                committer = std::make_unique<CachedCommitStudy>(ctx.device(), runtime.gpu());
                receipt["commit_pipeline_prepare_ms"] = committer->prepare(builder_mode);
                if (phase == "committed") {
                    candidate_field = std::make_unique<Field>(ctx.device(), runtime.gpu());
                    const auto stats =
                        committer->build(*candidate_field, *base_field, p, builder_mode);
                    receipt["initial_candidate_builder_ms"] = stats.wall_ms;
                }
            }
            receipt["initial_field"] = {{"bytes", base_field->stats().allocation_bytes},
                                        {"build_ms", base_field->stats().build_ms},
                                        {"filter_gpu_ms", base_field->stats().filter_gpu_ms},
                                        {"brick_gpu_ms", base_field->stats().brick_gpu_ms}};
            const auto slots = host::headless_capture_frame_slot_count(config.common);
            const bool active = phase == "preview" && pending;
            if (active)
                edit = controls(ctx.device(), p.controls);
            main_renderer.create(ctx.device(), ctx.gpu(), ctx.render_target().extent,
                                 ctx.render_target().format, slots,
                                 phase == "committed" && candidate_field ? candidate_field.get()
                                                                         : base_field.get(),
                                 config, phase == "committed" || active ? &p.edited : &p.base,
                                 nullptr, {}, nullptr, active ? edit.get() : nullptr);
            main_created = true;
            receipt["render_path"] =
                active ? "cached plus one primitive interval" : "existing Cached pipeline";
            if (sequence) {
                edit = controls(ctx.device(), p.controls);
                preview_renderer.create(ctx.device(), ctx.gpu(), ctx.render_target().extent,
                                        ctx.render_target().format, slots, base_field.get(), config,
                                        &p.edited, nullptr, {}, nullptr, edit.get());
                preview_created = true;
                const auto start = Clock::now();
                candidate_renderer.create(ctx.device(), ctx.gpu(), ctx.render_target().extent,
                                          ctx.render_target().format, slots, base_field.get(),
                                          config, &p.edited);
                candidate_created = true;
                receipt["candidate_renderer_prewarm_ms"] = elapsed(start);
            }
        };
        callbacks.before_frame = [&](host::HeadlessPngContext& ctx,
                                     const host::HeadlessCaptureFrame& frame) {
            if (!sequence)
                return;
            finish_frame();
            if (frame.index == 1)
                pending = true;
            if (frame.index >= 2 && !published) {
                commit_started = Clock::now();
                Json attempt{{"frame", frame.index}, {"pending_before", pending ? 1 : 0}};
                const bool injected = fault && frame.index == 2;
                if (injected &&
                    (failure_kind == "allocation" || failure_kind == "cancel-before-submit")) {
                    attempt.update({{"published", false},
                                    {"failure", failure_kind},
                                    {"candidate_ready_ms", elapsed(*commit_started)},
                                    {"submitted_gpu_work", false},
                                    {"pending_after", pending ? 1 : 0}});
                    receipt["commits"].push_back(std::move(attempt));
                } else {
                    candidate_field = std::make_unique<Field>(ctx.device(), runtime.gpu());
                    const auto allocated = elapsed(*commit_started);
                    if (committer) {
                        const auto stats = committer->build(*candidate_field, *base_field,
                                                            programs[0], builder_mode);
                        attempt.update({{"admission_ms", stats.admission_ms},
                                        {"reuse_gpu_ms", stats.reuse_gpu_ms},
                                        {"fallback_gpu_ms", stats.fallback_gpu_ms},
                                        {"work_counts", stats.work},
                                        {"full_fallback", stats.full_fallback},
                                        {"builder_working_bytes", committer->working_bytes()}});
                    } else
                        candidate_field->build(programs[0].edited);
                    const auto built = elapsed(*commit_started);
                    attempt.update({{"allocation_ms", allocated},
                                    {"field_build_ms", candidate_field->stats().build_ms},
                                    {"filter_gpu_ms", candidate_field->stats().filter_gpu_ms},
                                    {"brick_gpu_ms", candidate_field->stats().brick_gpu_ms},
                                    {"candidate_ready_ms", built},
                                    {"candidate_bytes", candidate_field->stats().allocation_bytes},
                                    {"peak_field_payload_bytes",
                                     base_field->stats().allocation_bytes +
                                         candidate_field->stats().allocation_bytes +
                                         (committer ? committer->working_bytes() : 0)}});
                    // Serialized identity injection tests the publication decision,
                    // not cancellation of a running GPU dispatch or live input.
                    const unsigned requested_branch = 1,
                                   candidate_branch = injected && failure_kind == "stale" ? 0U : 1U;
                    attempt.update({{"requested_branch", requested_branch},
                                    {"candidate_branch", candidate_branch},
                                    {"submitted_gpu_work", true}});
                    if ((injected && failure_kind == "discard-after-submit") ||
                        candidate_branch != requested_branch) {
                        attempt.update({{"published", false},
                                        {"failure", failure_kind},
                                        {"retained_base_geometry_hash",
                                         authored_geometry_hash(programs[0].base)},
                                        {"pending_after", pending ? 1 : 0}});
                        candidate_field.reset();
                    } else {
                        candidate_renderer.update_field(ctx.device(), *candidate_field);
                        published = true;
                        attempt.update(
                            {{"published", true},
                             {"publication_prepare_ms", elapsed(*commit_started) - built},
                             {"ready_to_render_ms", elapsed(*commit_started)},
                             {"published_geometry_hash",
                              authored_geometry_hash(programs[0].edited)},
                             {"pending_retained_until_frame_completion", pending ? 1 : 0}});
                    }
                    receipt["commits"].push_back(std::move(attempt));
                }
            }
            const auto display = published ? "committed" : frame.index == 0 ? "base" : "preview";
            receipt["frames"].push_back(
                {{"frame", frame.index},
                 {"display", display},
                 {"pending_before_frame", pending ? 1 : 0},
                 {"render_pending_operations", std::string_view(display) == "preview" ? 1 : 0}});
        };
        callbacks.record_frame = [&](host::HeadlessPngContext& ctx,
                                     const host::HeadlessCaptureFrame& frame,
                                     VkCommandBuffer command,
                                     const host::HeadlessRenderTarget& target) {
            if (query) {
                render::record_render_target_pass(vulkan::CommandRecorder(command),
                                                  render::render_target_view(target),
                                                  {.color = render::color_clear_value(0, 0, 0, 1)},
                                                  [](const vulkan::CommandRecorder&) {});
                return;
            }
            auto& renderer = sequence ? (published          ? candidate_renderer
                                         : frame.index == 0 ? main_renderer
                                                            : preview_renderer)
                                      : main_renderer;
            renderer.record(ctx.device(), command, target, frame.frame_slot, false, config,
                            config.yaw, config.pitch, config.distance, ctx.profile_recorder(),
                            frame.index);
            if (sequence) {
                vulkan::transition_image_layout(
                    command, vulkan::finish_color_attachment_for_readback_transition(target.image));
                readback = runtime.gpu().enqueue_rgba8_image_readback(
                    target.image, target.extent, "cached edit completed frame");
            }
        };
        callbacks.shutdown = [&](host::HeadlessPngContext&) {
            if (sequence)
                finish_frame();
            if (candidate_created)
                candidate_renderer.destroy();
            if (preview_created)
                preview_renderer.destroy();
            if (main_created)
                main_renderer.destroy();
            candidate_field.reset();
            base_field.reset();
            committer.reset();
            edit.reset();
            runtime.detach_gpu_if_attached();
            auto retained = output_path;
            retained.replace_extension("retained-source.json");
            save(retained, Json::parse(serialize_authored_scene(programs[0].edited)));
            receipt["retained_source_file"] = retained.string();
            save(output_path, receipt);
        };
        if (query) {
            config.common.headless_render_only = true;
            config.common.frames = 1;
            config.common.width = config.common.height = 16;
        }
        return host::HeadlessPngHost(
                   {.run_config = config.common,
                    .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
                   std::move(callbacks))
            .run();
    } catch (const std::exception& e) {
        std::cerr << "cached edit study: " << e.what() << '\n';
        return 1;
    }
}
