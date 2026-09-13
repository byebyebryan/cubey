#include <cubey/engine/gltf_scene_importer.h>

#include <cubey/render/deformation.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/staging_pool.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey {
namespace {

[[nodiscard]] render::MaterialHandle staging_material_handle(std::size_t index) {
    return {.index = static_cast<std::uint32_t>(index + 1U), .generation = 0U};
}

[[nodiscard]] render::MeshHandle staging_mesh_handle(std::size_t index) {
    return {.index = static_cast<std::uint32_t>(index + 1U), .generation = 0U};
}

[[nodiscard]] render::MaterialHandle material_handle_for_index(const GltfSceneResident& resident,
                                                               std::uint32_t index) {
    if (index >= resident.material_handles.size()) {
        throw std::runtime_error("glTF primitive material index is out of range");
    }
    return resident.material_handles[index];
}

[[nodiscard]] std::vector<render::SampledImageMaterialBinding>
material_sampled_image_bindings(const GltfSceneImportResources& resources,
                                const GltfPreparedMaterial& material) {
    if (!resources.default_textures.has_value()) {
        throw std::runtime_error("glTF material upload requires default textures");
    }
    std::vector<render::SampledImageMaterialBinding> bindings;
    bindings.reserve(material.textures.size());
    for (const GltfPreparedMaterialTexture& texture_ref : material.textures) {
        const render::Texture2D* texture = nullptr;
        if (texture_ref.texture_index == asset::kInvalidAssetIndex) {
            texture = &render::pbr_default_texture(resources.default_textures.value(),
                                                   texture_ref.binding);
        } else {
            if (texture_ref.texture_index >= resources.textures.size()) {
                throw std::runtime_error("glTF material prepared texture index is out of range");
            }
            texture = &resources.textures[texture_ref.texture_index];
        }
        bindings.push_back({
            .binding = static_cast<std::uint32_t>(texture_ref.binding),
            .sampler = texture->sampler().handle(),
            .image_view = texture->view(),
        });
    }
    return bindings;
}

[[nodiscard]] std::vector<render::UploadedTexture2DMip>
texture_mips(const GltfPreparedTexture& texture) {
    if (!texture.mips.empty()) {
        return texture.mips;
    }
    std::vector<render::UploadedTexture2DMip> mips;
    mips.reserve(texture.mip_levels);
    VkDeviceSize offset = 0;
    for (std::uint32_t mip = 0; mip < texture.mip_levels; ++mip) {
        const VkExtent2D extent = render::texture_2d_mip_extent(texture.extent, mip);
        const std::size_t byte_count = render::texture_2d_byte_size(extent, 1, texture.format);
        mips.push_back({.extent = extent, .byte_offset = offset, .byte_count = byte_count});
        offset += static_cast<VkDeviceSize>(byte_count);
    }
    return mips;
}

[[nodiscard]] std::vector<render::UploadedTexture2DMip> default_texture_mips() {
    return {{.extent = {1, 1}, .byte_offset = 0, .byte_count = 4}};
}

[[nodiscard]] std::uint32_t deformation_flags(GltfPrimitiveDeformationKind kind) {
    std::uint32_t flags = 0U;
    if (kind == GltfPrimitiveDeformationKind::Morph ||
        kind == GltfPrimitiveDeformationKind::MorphSkin) {
        flags |= static_cast<std::uint32_t>(render::GpuDeformationFlags::Morph);
    }
    if (kind == GltfPrimitiveDeformationKind::Skin ||
        kind == GltfPrimitiveDeformationKind::MorphSkin) {
        flags |= static_cast<std::uint32_t>(render::GpuDeformationFlags::Skin);
    }
    return flags;
}

[[nodiscard]] bool deformation_has_skin(GltfPrimitiveDeformationKind kind) {
    return kind == GltfPrimitiveDeformationKind::Skin ||
           kind == GltfPrimitiveDeformationKind::MorphSkin;
}

template <typename T> [[nodiscard]] VkDeviceSize span_byte_size(std::span<const T> values) {
    if (values.empty()) {
        throw std::runtime_error("glTF deformation buffer byte size must be positive");
    }
    return static_cast<VkDeviceSize>(values.size() * sizeof(T));
}

template <typename T>
[[nodiscard]] vulkan::Buffer create_host_storage_buffer(const vulkan::Device& device,
                                                        std::span<const T> initial_values) {
    const VkDeviceSize byte_count = span_byte_size(initial_values);
    vulkan::Buffer buffer(device, vulkan::BufferConfig{
                                      .size = byte_count,
                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                      .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  });
    buffer.upload(initial_values.data(), byte_count);
    return buffer;
}

[[nodiscard]] std::uint32_t deformation_binding(render::GpuDeformationBinding value) {
    return static_cast<std::uint32_t>(value);
}

void update_deformation_descriptors(const vulkan::Device& device,
                                    GltfDeformationPrimitiveResources& resource) {
    if (!resource.base_vertices || !resource.morph_targets || !resource.skin_influences ||
        resource.descriptor_sets == nullptr) {
        throw std::runtime_error("glTF deformation descriptors require initialized buffers");
    }
    for (std::uint32_t frame_index = 0; frame_index < resource.descriptor_sets->size();
         ++frame_index) {
        const VkDescriptorSet set = resource.descriptor_sets->set(frame_index);
        vulkan::DescriptorWriteBatch writes;
        writes
            .storage_buffer(set, deformation_binding(render::GpuDeformationBinding::BaseVertices),
                            resource.base_vertices->handle(), resource.base_vertices->size())
            .storage_buffer(set, deformation_binding(render::GpuDeformationBinding::MorphTargets),
                            resource.morph_targets->handle(), resource.morph_targets->size())
            .storage_buffer(set, deformation_binding(render::GpuDeformationBinding::MorphWeights),
                            resource.morph_weights.at(frame_index).handle(),
                            resource.morph_weights.at(frame_index).size())
            .storage_buffer(set, deformation_binding(render::GpuDeformationBinding::SkinInfluences),
                            resource.skin_influences->handle(), resource.skin_influences->size())
            .storage_buffer(set, deformation_binding(render::GpuDeformationBinding::JointPalette),
                            resource.joint_palettes.at(frame_index).handle(),
                            resource.joint_palettes.at(frame_index).size())
            .storage_buffer(set, deformation_binding(render::GpuDeformationBinding::OutputVertices),
                            resource.output_meshes.at(frame_index).vertex_buffer().handle(),
                            resource.output_meshes.at(frame_index).vertex_buffer().size());
        writes.update(device);
    }
}

} // namespace

struct GltfSceneUploadSession::Impl : public std::enable_shared_from_this<Impl> {
    enum class Phase {
        DefaultTextures,
        Textures,
        Materials,
        Meshes,
        Deformation,
        FinishedRecording,
        Complete,
        Failed,
        Abandoned,
    };

    struct TextureTask {
        std::span<const std::uint8_t> source{};
        VkExtent2D extent{1, 1};
        std::uint32_t mip_levels = 1;
        VkFormat format = VK_FORMAT_UNDEFINED;
        vulkan::SamplerConfig sampler{};
        std::vector<render::UploadedTexture2DMip> mips{};
        std::optional<render::Texture2D> texture{};
        std::uint32_t mip_index = 0;
        std::uint32_t block_row = 0;
        bool transitioned = false;
    };

    struct MeshTask {
        const GltfPreparedMeshPrimitive* source = nullptr;
        std::optional<vulkan::Buffer> vertex{};
        std::optional<vulkan::Buffer> index{};
        VkDeviceSize vertex_offset = 0;
        VkDeviceSize index_offset = 0;
    };

    struct DeformationTask {
        enum class Stage {
            CreateBaseVertices,
            UploadBaseVertices,
            CreateMorphTargets,
            UploadMorphTargets,
            CreateSkinInfluences,
            UploadSkinInfluences,
            CreateFrameStorage,
            CreateOutputVertex,
            UploadOutputVertex,
            CreateOutputIndex,
            UploadOutputIndex,
            FinishOutput,
            CreateDescriptors,
        };

        const GltfPreparedDeformationPrimitive* source = nullptr;
        const GltfPreparedMeshPrimitive* geometry = nullptr;
        std::size_t resource_index = 0;
        std::uint32_t frame_index = 0;
        std::optional<vulkan::Buffer> output_vertex{};
        std::optional<vulkan::Buffer> output_index{};
        VkDeviceSize base_vertex_offset = 0;
        VkDeviceSize morph_target_offset = 0;
        VkDeviceSize skin_influence_offset = 0;
        VkDeviceSize output_vertex_offset = 0;
        VkDeviceSize output_index_offset = 0;
        Stage stage = Stage::CreateBaseVertices;
    };

    enum class AdvanceResult {
        Progress,
        StepFull,
        Backpressure,
    };

    struct StagingAttempt {
        enum class Result {
            Staged,
            StepFull,
            Backpressure,
        };

        Result result = Result::Backpressure;
        vulkan::GpuUploadStagingSlice slice{};
    };

    // One owner callback may aggregate several bounded copy operations. It
    // deliberately owns only one GpuUploadStep, so all copies recorded in an
    // app-frame advance become one same-queue physical submission.
    struct OwnerStepAccumulator {
        OwnerStepAccumulator(vulkan::GpuOwnerContext& owner, vulkan::GpuUploadStepConfig config)
            : owner_(&owner), config_(std::move(config)) {}

        [[nodiscard]] StagingAttempt try_stage(std::span<const std::byte> bytes,
                                               VkDeviceSize alignment) {
            const VkDeviceSize byte_count = static_cast<VkDeviceSize>(bytes.size());
            if (byte_count > config_.max_staged_byte_size - staged_byte_count_) {
                return {.result = StagingAttempt::Result::StepFull};
            }
            if (step_ == nullptr) {
                step_ = std::make_unique<vulkan::GpuUploadStep>(*owner_, config_);
            }
            const std::optional<vulkan::GpuUploadStagingSlice> slice =
                step_->try_stage_copy(bytes, alignment);
            if (!slice.has_value()) {
                // Keep earlier recordings intact: the caller submits them,
                // then retries this untouched operation after retirement. An
                // empty lexical step can be released immediately.
                if (staged_byte_count_ == 0U) {
                    step_.reset();
                }
                return {.result = StagingAttempt::Result::Backpressure};
            }
            staged_byte_count_ += byte_count;
            return {.result = StagingAttempt::Result::Staged, .slice = slice.value()};
        }

        void add_copy_count() {
            step_->add_copy_count();
        }

        [[nodiscard]] VkCommandBuffer command_buffer() const {
            return step_->command_buffer();
        }

        void mark_static_mesh_copy() {
            has_static_mesh_copy_ = true;
        }

        [[nodiscard]] bool has_copies() const noexcept {
            return step_ != nullptr;
        }

        [[nodiscard]] bool has_static_mesh_copy() const noexcept {
            return has_static_mesh_copy_;
        }

        [[nodiscard]] vulkan::GpuUploadStepTicket submit() {
            return step_->submit(*owner_, "vkQueueSubmit glTF upload session step");
        }

      private:
        vulkan::GpuOwnerContext* owner_ = nullptr;
        vulkan::GpuUploadStepConfig config_{};
        std::unique_ptr<vulkan::GpuUploadStep> step_{};
        VkDeviceSize staged_byte_count_ = 0;
        bool has_static_mesh_copy_ = false;
    };

    explicit Impl(std::shared_ptr<const GltfPreparedScene> prepared_value,
                  GltfSceneImportConfig config_value, vulkan::GpuRuntime& runtime_value)
        : prepared(std::move(prepared_value)), config(std::move(config_value)) {
        if (prepared == nullptr) {
            throw std::runtime_error("glTF upload session requires prepared scene ownership");
        }
        if (config.frame_slot_count == 0) {
            throw std::runtime_error("glTF scene import requires at least one frame slot");
        }
        if (!std::isfinite(config.upload_policy.owner_cpu_target_milliseconds) ||
            config.upload_policy.owner_cpu_target_milliseconds <= 0.0) {
            throw std::runtime_error("glTF upload owner CPU target must be positive");
        }
        if (config.upload_policy.step_byte_cap == 0U) {
            throw std::runtime_error("glTF upload step byte cap must be positive");
        }
        if (config.upload_policy.copy_byte_target == 0U) {
            throw std::runtime_error("glTF upload copy byte target must be positive");
        }
        if (config.upload_policy.copy_byte_target > config.upload_policy.step_byte_cap) {
            throw std::runtime_error(
                "glTF upload copy byte target must not exceed the step byte cap");
        }
        if (!runtime_value.has_staging_pool()) {
            throw std::runtime_error("glTF upload session requires a configured GPU staging pool");
        }
        metrics.owner_target_milliseconds = config.upload_policy.owner_cpu_target_milliseconds;
        metrics.step_byte_cap = config.upload_policy.step_byte_cap;
        metrics.copy_byte_target = config.upload_policy.copy_byte_target;
        resident.resources.mesh_primitives.resize(prepared->meshes.size());
    }

    void arm_owner_cleanup(vulkan::GpuRuntime& runtime_value) {
        const std::shared_ptr<Impl> self = shared_from_this();
        owner_cleanup = runtime_value.register_owner_cleanup(
            "retire abandoned glTF upload session", [self](vulkan::GpuOwnerContext& owner) {
                self->retire_abandoned_resident_on_owner_thread(owner);
            });
    }

    void record_one_owner_step(vulkan::GpuOwnerContext& owner) {
        owner.require_owner_thread("glTF upload session requires the GPU owner thread");
        std::unique_lock lock(mutex);
        if (abandoned || phase == Phase::Failed || phase == Phase::Complete ||
            phase == Phase::Abandoned || phase == Phase::FinishedRecording) {
            lock.unlock();
            owner_step_in_flight.store(false, std::memory_order_release);
            return;
        }
        const Clock::time_point started = Clock::now();
        backpressure_pending = false;
        try {
            OwnerStepAccumulator step(owner, upload_step_config());
            for (;;) {
                const AdvanceResult result = advance_one_owner_operation(owner, step);
                if (result == AdvanceResult::Backpressure) {
                    ++metrics.backpressure_count;
                    backpressure_pending = true;
                    break;
                }
                if (result == AdvanceResult::StepFull || phase == Phase::FinishedRecording ||
                    phase == Phase::Failed || phase == Phase::Complete ||
                    phase == Phase::Abandoned ||
                    elapsed_milliseconds(started) >= metrics.owner_target_milliseconds) {
                    break;
                }
            }
            if (step.has_copies()) {
                record_submitted_step(owner, step.submit());
                if (step.has_static_mesh_copy()) {
                    ++resident.mesh_upload_transfer_submission_count;
                }
            }
        } catch (const std::exception& error) {
            fail(error.what());
        } catch (...) {
            fail("unknown glTF upload session owner-step failure");
        }
        const double elapsed = elapsed_milliseconds(started);
        ++metrics.owner_advance_count;
        metrics.owner_total_milliseconds += elapsed;
        metrics.owner_max_step_milliseconds =
            std::max(metrics.owner_max_step_milliseconds, elapsed);
        if (elapsed > metrics.owner_target_milliseconds) {
            ++metrics.owner_over_target_step_count;
        }
        resident.upload_session_metrics = metrics;
        lock.unlock();
        // The non-wait poll path reads this before taking `mutex`; publish it
        // only after all owner state is stable and the mutex is released.
        owner_step_in_flight.store(false, std::memory_order_release);
    }

    [[nodiscard]] AdvanceResult advance_one_owner_operation(vulkan::GpuOwnerContext& owner,
                                                            OwnerStepAccumulator& step) {
        switch (phase) {
        case Phase::DefaultTextures:
            return record_default_texture(owner, step);
        case Phase::Textures:
            return record_prepared_texture(owner, step);
        case Phase::Materials:
            return record_material(owner);
        case Phase::Meshes:
            return record_mesh(owner, step);
        case Phase::Deformation:
            return record_deformation(owner, step);
        case Phase::FinishedRecording:
        case Phase::Complete:
        case Phase::Failed:
        case Phase::Abandoned:
            return AdvanceResult::Progress;
        }
        return AdvanceResult::Progress;
    }

    [[nodiscard]] AdvanceResult record_default_texture(vulkan::GpuOwnerContext& owner,
                                                       OwnerStepAccumulator& step) {
        const std::span<const render::PbrDefaultTextureSpec> specs =
            render::pbr_default_texture_specs();
        if (default_texture_index >= specs.size()) {
            resident.resources.default_textures.emplace(
                render::make_pbr_default_texture_set(std::move(default_textures)));
            phase = Phase::Textures;
            return AdvanceResult::Progress;
        }
        if (!texture_task.has_value()) {
            const render::PbrDefaultTextureSpec& spec = specs[default_texture_index];
            texture_task.emplace(TextureTask{
                .source = {spec.rgba8.data(), spec.rgba8.size()},
                .extent = {1, 1},
                .mip_levels = 1,
                .format = spec.format,
                .sampler = {},
                .mips = default_texture_mips(),
            });
        }
        bool texture_finished = false;
        const AdvanceResult result =
            record_texture_copy(owner, step, texture_task.value(), texture_finished);
        if (result != AdvanceResult::Progress) {
            return result;
        }
        if (texture_finished) {
            default_textures.push_back(std::move(texture_task->texture.value()));
            texture_task.reset();
            ++default_texture_index;
        }
        return AdvanceResult::Progress;
    }

    [[nodiscard]] AdvanceResult record_prepared_texture(vulkan::GpuOwnerContext& owner,
                                                        OwnerStepAccumulator& step) {
        if (prepared_texture_index >= prepared->textures.size()) {
            phase = Phase::Materials;
            return AdvanceResult::Progress;
        }
        if (!texture_task.has_value()) {
            const GltfPreparedTexture& source = prepared->textures[prepared_texture_index];
            texture_task.emplace(TextureTask{
                .source = {source.bytes.data(), source.bytes.size()},
                .extent = source.extent,
                .mip_levels = source.mip_levels,
                .format = source.format,
                .sampler = source.sampler,
                .mips = texture_mips(source),
            });
        }
        bool texture_finished = false;
        const AdvanceResult result =
            record_texture_copy(owner, step, texture_task.value(), texture_finished);
        if (result != AdvanceResult::Progress) {
            return result;
        }
        if (texture_finished) {
            resident.resources.textures.push_back(std::move(texture_task->texture.value()));
            texture_task.reset();
            ++prepared_texture_index;
        }
        return AdvanceResult::Progress;
    }

    [[nodiscard]] AdvanceResult record_texture_copy(vulkan::GpuOwnerContext& owner,
                                                    OwnerStepAccumulator& step, TextureTask& task,
                                                    bool& texture_finished) {
        texture_finished = false;
        if (!task.texture.has_value()) {
            task.texture.emplace(owner.device(),
                                 render::Texture2DConfig{
                                     .extent = task.extent,
                                     .mip_levels = task.mip_levels,
                                     .format = task.format,
                                     .usage = render::Texture2DUsage::TransferSampled,
                                     .create_sampler = true,
                                     .sampler = task.sampler,
                                 });
            return AdvanceResult::Progress;
        }
        if (task.mip_index >= task.mips.size()) {
            texture_finished = true;
            return AdvanceResult::Progress;
        }
        const render::UploadedTexture2DMip& mip = task.mips[task.mip_index];
        const render::TextureFormatLayout layout = render::texture_format_layout(task.format);
        const std::uint32_t blocks_x =
            (mip.extent.width + layout.block_width - 1U) / layout.block_width;
        const std::uint32_t blocks_y =
            (mip.extent.height + layout.block_height - 1U) / layout.block_height;
        const VkDeviceSize bytes_per_row =
            static_cast<VkDeviceSize>(blocks_x) * static_cast<VkDeviceSize>(layout.bytes_per_block);
        if (bytes_per_row == 0) {
            throw std::runtime_error("glTF texture upload row size is zero");
        }
        const std::uint32_t remaining_rows = blocks_y - task.block_row;
        const std::uint32_t target_rows = std::max(
            1U, static_cast<std::uint32_t>(config.upload_policy.copy_byte_target / bytes_per_row));
        const std::uint32_t row_count = std::min(remaining_rows, target_rows);
        const VkDeviceSize byte_count = bytes_per_row * static_cast<VkDeviceSize>(row_count);
        const VkDeviceSize byte_offset =
            mip.byte_offset + bytes_per_row * static_cast<VkDeviceSize>(task.block_row);
        if (byte_offset > task.source.size() || byte_count > task.source.size() - byte_offset) {
            throw std::runtime_error("glTF texture chunk exceeds prepared bytes");
        }

        const StagingAttempt attempt = step.try_stage(
            {reinterpret_cast<const std::byte*>(task.source.data() + byte_offset),
             static_cast<std::size_t>(byte_count)},
            std::max<VkDeviceSize>(4U, static_cast<VkDeviceSize>(layout.bytes_per_block)));
        if (attempt.result == StagingAttempt::Result::StepFull) {
            return AdvanceResult::StepFull;
        }
        if (attempt.result == StagingAttempt::Result::Backpressure) {
            return AdvanceResult::Backpressure;
        }
        if (!task.transitioned) {
            vulkan::transition_image_layout(
                step.command_buffer(),
                vulkan::begin_transfer_dst_transition(task.texture->handle(), task.mip_levels, 1));
            task.transitioned = true;
        }
        const std::uint32_t image_y = task.block_row * layout.block_height;
        const std::uint32_t copy_height =
            std::min(mip.extent.height - image_y, row_count * layout.block_height);
        const VkBufferImageCopy copy = vulkan::buffer_image_copy({
            .extent = {mip.extent.width, copy_height, 1},
            .buffer_offset = attempt.slice.offset,
            .mip_level = task.mip_index,
            .image_offset = {0, static_cast<std::int32_t>(image_y), 0},
        });
        vkCmdCopyBufferToImage(step.command_buffer(), attempt.slice.buffer, task.texture->handle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        step.add_copy_count();
        task.block_row += row_count;
        const bool mip_finished = task.block_row == blocks_y;
        if (mip_finished) {
            task.block_row = 0;
            ++task.mip_index;
        }
        texture_finished = task.mip_index == task.mips.size();
        if (texture_finished) {
            vulkan::transition_image_layout(step.command_buffer(),
                                            vulkan::finish_transfer_dst_for_sampling_transition(
                                                task.texture->handle(), task.mip_levels, 1));
        }
        return AdvanceResult::Progress;
    }

    [[nodiscard]] AdvanceResult record_material(vulkan::GpuOwnerContext& owner) {
        if (material_index >= prepared->materials.size()) {
            phase = Phase::Meshes;
            return AdvanceResult::Progress;
        }
        const GltfPreparedMaterial& material = prepared->materials[material_index];
        const render::MaterialHandle handle = staging_material_handle(material_index);
        resident.resources.materials.set_factors(handle, material.factors);
        resident.resources.materials.emplace_instance(
            handle, owner.device(),
            render::FrameUniformMaterialInstanceConfig{
                .material_pass = render::pbr_forward_pass_info(),
                .descriptor_set = 1,
                .frame_slot_count = config.frame_slot_count,
                .uniform_binding = static_cast<std::uint32_t>(render::PbrMaterialBinding::Uniforms),
                .sampled_images = material_sampled_image_bindings(resident.resources, material),
            });
        resident.material_handles.push_back(handle);
        ++material_index;
        return AdvanceResult::Progress;
    }

    [[nodiscard]] AdvanceResult record_mesh(vulkan::GpuOwnerContext& owner,
                                            OwnerStepAccumulator& step) {
        while (mesh_index < prepared->meshes.size() &&
               primitive_index >= prepared->meshes[mesh_index].primitives.size()) {
            ++mesh_index;
            primitive_index = 0;
        }
        if (mesh_index >= prepared->meshes.size()) {
            initialize_deformation_primitives();
            phase = Phase::Deformation;
            return AdvanceResult::Progress;
        }
        const GltfPreparedMeshPrimitive& source =
            prepared->meshes[mesh_index].primitives[primitive_index];
        if (!mesh_task.has_value()) {
            mesh_task.emplace(MeshTask{.source = &source});
        }
        MeshTask& task = mesh_task.value();
        if (!task.vertex.has_value()) {
            task.vertex.emplace(owner.device(),
                                vulkan::device_local_buffer_config(
                                    static_cast<VkDeviceSize>(task.source->vertices.size() *
                                                              sizeof(render::PbrVertex)),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            return AdvanceResult::Progress;
        }
        if (!task.index.has_value()) {
            task.index.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(
                    static_cast<VkDeviceSize>(task.source->indices.size() * sizeof(std::uint32_t)),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
            return AdvanceResult::Progress;
        }
        const VkDeviceSize vertex_bytes =
            static_cast<VkDeviceSize>(task.source->vertices.size() * sizeof(render::PbrVertex));
        const VkDeviceSize index_bytes =
            static_cast<VkDeviceSize>(task.source->indices.size() * sizeof(std::uint32_t));
        if (task.vertex_offset < vertex_bytes) {
            return record_buffer_copy(
                step,
                std::span<const std::byte>{
                    reinterpret_cast<const std::byte*>(task.source->vertices.data()),
                    static_cast<std::size_t>(vertex_bytes)},
                task.vertex.value(), task.vertex_offset, true);
        }
        if (task.index_offset < index_bytes) {
            return record_buffer_copy(
                step,
                std::span<const std::byte>{
                    reinterpret_cast<const std::byte*>(task.source->indices.data()),
                    static_cast<std::size_t>(index_bytes)},
                task.index.value(), task.index_offset, true);
        }
        const render::MeshHandle handle = staging_mesh_handle(resident.static_mesh_handles.size());
        resident.resources.meshes.emplace(
            handle,
            render::Mesh::from_uploaded_buffers(
                std::move(task.vertex.value()), std::move(task.index.value()), VK_INDEX_TYPE_UINT32,
                static_cast<std::uint32_t>(task.source->indices.size())));
        resident.static_mesh_handles.push_back(handle);
        resident.resources.mesh_primitives[mesh_index].push_back({
            .mesh = handle,
            .material = material_handle_for_index(resident, task.source->material_index),
            .local_bounds = task.source->local_bounds,
            .mesh_index = static_cast<std::uint32_t>(mesh_index),
            .primitive_index = static_cast<std::uint32_t>(primitive_index),
            .deformation = GltfPrimitiveDeformationKind::Static,
        });
        mesh_task.reset();
        ++primitive_index;
        return AdvanceResult::Progress;
    }

    [[nodiscard]] AdvanceResult record_buffer_copy(OwnerStepAccumulator& step,
                                                   std::span<const std::byte> source,
                                                   vulkan::Buffer& destination,
                                                   VkDeviceSize& offset,
                                                   bool count_static_mesh_bytes) {
        const VkDeviceSize remaining = static_cast<VkDeviceSize>(source.size()) - offset;
        const VkDeviceSize byte_count = std::min(remaining, config.upload_policy.copy_byte_target);
        const StagingAttempt attempt = step.try_stage(
            source.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(byte_count)),
            4U);
        if (attempt.result == StagingAttempt::Result::StepFull) {
            return AdvanceResult::StepFull;
        }
        if (attempt.result == StagingAttempt::Result::Backpressure) {
            return AdvanceResult::Backpressure;
        }
        const VkBufferCopy copy{
            .srcOffset = attempt.slice.offset, .dstOffset = offset, .size = byte_count};
        vkCmdCopyBuffer(step.command_buffer(), attempt.slice.buffer, destination.handle(), 1,
                        &copy);
        step.add_copy_count();
        offset += byte_count;
        if (count_static_mesh_bytes) {
            resident.mesh_upload_byte_count += byte_count;
            step.mark_static_mesh_copy();
        }
        return AdvanceResult::Progress;
    }

    void initialize_deformation_primitives() {
        if (deformation_initialized) {
            return;
        }
        GltfSceneImportResources& resources = resident.resources;
        resources.deformable_primitives.reserve(prepared->deformable_primitives.size());
        resources.deformation.frame_meshes.resize(config.frame_slot_count);
        for (const GltfPreparedDeformationPrimitive& source : prepared->deformable_primitives) {
            if (source.mesh_index >= resources.mesh_primitives.size() ||
                source.primitive_index >= resources.mesh_primitives[source.mesh_index].size()) {
                throw std::runtime_error(
                    "prepared glTF deformation mesh primitive is out of range");
            }
            const GltfImportedPrimitive3D& static_source =
                resources.mesh_primitives[source.mesh_index][source.primitive_index];
            const render::MeshHandle output = staging_mesh_handle(
                resident.static_mesh_handles.size() + resident.deformation_mesh_handles.size());
            resident.deformation_mesh_handles.push_back(output);
            resources.deformable_primitives.push_back({
                .node_index = source.node_index,
                .mesh_index = source.mesh_index,
                .primitive_index = source.primitive_index,
                .skin_index = source.skin_index,
                .deformation = source.deformation,
                .output_mesh = output,
                .material = static_source.material,
                .local_bounds = static_source.local_bounds,
            });
        }
        resources.deformation.primitives.reserve(prepared->deformable_primitives.size());
        deformation_initialized = true;
    }

    [[nodiscard]] AdvanceResult record_deformation(vulkan::GpuOwnerContext& owner,
                                                   OwnerStepAccumulator& step) {
        initialize_deformation_primitives();
        if (deformation_index >= prepared->deformable_primitives.size()) {
            if (!prepared->deformable_primitives.empty() &&
                resident.resources.deformation.pipeline == nullptr) {
                if (config.deformation_compute_shader.empty()) {
                    throw std::runtime_error("glTF deformation resources require a compute shader");
                }
                const std::array<VkDescriptorSetLayout, 1> layouts{
                    resident.resources.deformation.primitives.front().descriptor_sets->layout(),
                };
                resident.resources.deformation.pipeline =
                    std::make_unique<render::ComputePipelineResource>(
                        owner.device(), render::gpu_deformation_pipeline_config(
                                            config.deformation_compute_shader, layouts));
                return AdvanceResult::Progress;
            }
            phase = Phase::FinishedRecording;
            return AdvanceResult::Progress;
        }
        if (!deformation_task.has_value()) {
            const GltfPreparedDeformationPrimitive& source =
                prepared->deformable_primitives[deformation_index];
            if (source.mesh_index >= prepared->meshes.size() ||
                source.primitive_index >= prepared->meshes[source.mesh_index].primitives.size()) {
                throw std::runtime_error(
                    "prepared glTF deformation geometry primitive is out of range");
            }
            const GltfPreparedMeshPrimitive& geometry =
                prepared->meshes[source.mesh_index].primitives[source.primitive_index];
            if (geometry.vertices.empty() || geometry.indices.empty()) {
                throw std::runtime_error(
                    "prepared glTF deformation geometry requires indexed vertices");
            }
            GltfDeformationPrimitiveResources& resource =
                resident.resources.deformation.primitives.emplace_back();
            resource.primitive = resident.resources.deformable_primitives.at(deformation_index);
            resource.push_constants = {
                .vertex_count = static_cast<std::uint32_t>(geometry.vertices.size()),
                .morph_target_count =
                    source.morph_targets.empty()
                        ? 0U
                        : static_cast<std::uint32_t>(source.morph_targets.size() /
                                                     (geometry.vertices.size() * 9U)),
                .joint_count = deformation_has_skin(source.deformation)
                                   ? static_cast<std::uint32_t>(source.initial_joint_palette.size())
                                   : 0U,
                .flags = deformation_flags(source.deformation),
            };
            deformation_task.emplace(DeformationTask{
                .source = &source,
                .geometry = &geometry,
                .resource_index = resident.resources.deformation.primitives.size() - 1U,
            });
        }
        return record_deformation_task(owner, step, deformation_task.value());
    }

    [[nodiscard]] AdvanceResult record_deformation_task(vulkan::GpuOwnerContext& owner,
                                                        OwnerStepAccumulator& step,
                                                        DeformationTask& task) {
        GltfDeformationPrimitiveResources& resource =
            resident.resources.deformation.primitives.at(task.resource_index);
        const GltfPreparedDeformationPrimitive& source = *task.source;
        const GltfPreparedMeshPrimitive& geometry = *task.geometry;
        const auto upload = [&](std::span<const std::byte> bytes, vulkan::Buffer& destination,
                                VkDeviceSize& offset) {
            return record_buffer_copy(step, bytes, destination, offset, false);
        };
        switch (task.stage) {
        case DeformationTask::Stage::CreateBaseVertices:
            resource.base_vertices.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(span_byte_size(std::span{geometry.vertices}),
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadBaseVertices;
            return AdvanceResult::Progress;
        case DeformationTask::Stage::UploadBaseVertices: {
            const VkDeviceSize total = span_byte_size(std::span{geometry.vertices});
            const AdvanceResult result =
                upload({reinterpret_cast<const std::byte*>(geometry.vertices.data()),
                        static_cast<std::size_t>(total)},
                       resource.base_vertices.value(), task.base_vertex_offset);
            if (result == AdvanceResult::Progress && task.base_vertex_offset == total) {
                task.stage = DeformationTask::Stage::CreateMorphTargets;
            }
            return result;
        }
        case DeformationTask::Stage::CreateMorphTargets:
            resource.morph_targets.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(span_byte_size(std::span{source.morph_targets}),
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadMorphTargets;
            return AdvanceResult::Progress;
        case DeformationTask::Stage::UploadMorphTargets: {
            const VkDeviceSize total = span_byte_size(std::span{source.morph_targets});
            const AdvanceResult result =
                upload({reinterpret_cast<const std::byte*>(source.morph_targets.data()),
                        static_cast<std::size_t>(total)},
                       resource.morph_targets.value(), task.morph_target_offset);
            if (result == AdvanceResult::Progress && task.morph_target_offset == total) {
                task.stage = DeformationTask::Stage::CreateSkinInfluences;
            }
            return result;
        }
        case DeformationTask::Stage::CreateSkinInfluences:
            resource.skin_influences.emplace(owner.device(),
                                             vulkan::device_local_buffer_config(
                                                 span_byte_size(std::span{source.skin_influences}),
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadSkinInfluences;
            return AdvanceResult::Progress;
        case DeformationTask::Stage::UploadSkinInfluences: {
            const VkDeviceSize total = span_byte_size(std::span{source.skin_influences});
            const AdvanceResult result =
                upload({reinterpret_cast<const std::byte*>(source.skin_influences.data()),
                        static_cast<std::size_t>(total)},
                       resource.skin_influences.value(), task.skin_influence_offset);
            if (result == AdvanceResult::Progress && task.skin_influence_offset == total) {
                task.stage = DeformationTask::Stage::CreateFrameStorage;
            }
            return result;
        }
        case DeformationTask::Stage::CreateFrameStorage:
            if (task.frame_index >= config.frame_slot_count) {
                task.frame_index = 0;
                task.stage = DeformationTask::Stage::CreateOutputVertex;
                return AdvanceResult::Progress;
            }
            resource.morph_weights.push_back(create_host_storage_buffer(
                owner.device(), std::span<const float>{source.initial_morph_weights}));
            resource.joint_palettes.push_back(create_host_storage_buffer(
                owner.device(), std::span<const math::Mat4>{source.initial_joint_palette}));
            ++task.frame_index;
            return AdvanceResult::Progress;
        case DeformationTask::Stage::CreateOutputVertex:
            if (task.frame_index >= config.frame_slot_count) {
                task.stage = DeformationTask::Stage::CreateDescriptors;
                return AdvanceResult::Progress;
            }
            task.output_vertex.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(span_byte_size(std::span{geometry.vertices}),
                                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadOutputVertex;
            return AdvanceResult::Progress;
        case DeformationTask::Stage::UploadOutputVertex: {
            const VkDeviceSize total = span_byte_size(std::span{geometry.vertices});
            const AdvanceResult result =
                upload({reinterpret_cast<const std::byte*>(geometry.vertices.data()),
                        static_cast<std::size_t>(total)},
                       task.output_vertex.value(), task.output_vertex_offset);
            if (result == AdvanceResult::Progress && task.output_vertex_offset == total) {
                task.stage = DeformationTask::Stage::CreateOutputIndex;
            }
            return result;
        }
        case DeformationTask::Stage::CreateOutputIndex:
            task.output_index.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(span_byte_size(std::span{geometry.indices}),
                                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadOutputIndex;
            return AdvanceResult::Progress;
        case DeformationTask::Stage::UploadOutputIndex: {
            const VkDeviceSize total = span_byte_size(std::span{geometry.indices});
            const AdvanceResult result =
                upload({reinterpret_cast<const std::byte*>(geometry.indices.data()),
                        static_cast<std::size_t>(total)},
                       task.output_index.value(), task.output_index_offset);
            if (result == AdvanceResult::Progress && task.output_index_offset == total) {
                task.stage = DeformationTask::Stage::FinishOutput;
            }
            return result;
        }
        case DeformationTask::Stage::FinishOutput:
            resource.output_meshes.push_back(render::Mesh::from_uploaded_buffers(
                std::move(task.output_vertex.value()), std::move(task.output_index.value()),
                VK_INDEX_TYPE_UINT32, static_cast<std::uint32_t>(geometry.indices.size())));
            task.output_vertex.reset();
            task.output_index.reset();
            task.output_vertex_offset = 0;
            task.output_index_offset = 0;
            ++task.frame_index;
            task.stage = DeformationTask::Stage::CreateOutputVertex;
            return AdvanceResult::Progress;
        case DeformationTask::Stage::CreateDescriptors:
            resource.descriptor_sets = std::make_unique<vulkan::DescriptorSetArray>(
                owner.device(),
                render::gpu_deformation_descriptor_set_info(config.frame_slot_count));
            update_deformation_descriptors(owner.device(), resource);
            for (std::uint32_t frame_index = 0; frame_index < config.frame_slot_count;
                 ++frame_index) {
                resident.resources.deformation.frame_meshes.bind(
                    {.index = frame_index, .count = config.frame_slot_count},
                    resource.primitive.output_mesh, &resource.output_meshes.at(frame_index));
            }
            deformation_task.reset();
            ++deformation_index;
            return AdvanceResult::Progress;
        }
        throw std::runtime_error("unknown glTF deformation upload stage");
    }

    void record_submitted_step(vulkan::GpuOwnerContext& owner, vulkan::GpuUploadStepTicket ticket) {
        const vulkan::GpuUploadStepMetrics step_metrics = ticket.metrics();
        const vulkan::GpuStagingPoolStats pool_stats = owner.staging_pool().stats();
        if (metrics.step_count == 0U) {
            metrics.pool_initial_capacity_byte_count = pool_stats.capacity_byte_size;
            pool_initial_block_count = pool_stats.block_count;
            first_submission_started.emplace(Clock::now());
        }
        ++metrics.step_count;
        ++metrics.submission_count;
        metrics.uploaded_byte_count += step_metrics.staged_byte_size;
        metrics.copy_count += step_metrics.copy_count;
        metrics.pool_final_capacity_byte_count = pool_stats.capacity_byte_size;
        metrics.pool_peak_capacity_byte_count =
            std::max(metrics.pool_peak_capacity_byte_count, pool_stats.capacity_byte_size);
        metrics.pool_reserved_at_final_submission_byte_count = pool_stats.reserved_byte_size;
        metrics.pool_growth_count = static_cast<std::uint32_t>(
            std::min<std::size_t>(std::numeric_limits<std::uint32_t>::max(),
                                  pool_stats.block_count - pool_initial_block_count));
        final_ticket = std::move(ticket);
        resident.final_upload_step = final_ticket.value();
    }

    void fail(std::string message) {
        phase = Phase::Failed;
        failure = std::move(message);
    }

    void record_terminal_failure(const char* message) noexcept {
        try {
            std::scoped_lock lock(mutex);
            if (phase != Phase::Complete && phase != Phase::Abandoned) {
                fail(message == nullptr ? "unknown glTF upload session failure" : message);
            }
        } catch (...) {
            // The caller is already propagating a terminal error. Do not hide
            // it if recording the diagnostic itself runs out of memory.
        }
    }

    void mark_complete() {
        if (phase == Phase::Complete) {
            return;
        }
        if (first_submission_started.has_value()) {
            metrics.first_step_to_final_completion_milliseconds =
                elapsed_milliseconds(first_submission_started.value());
        }
        resident.upload_session_metrics = metrics;
        phase = Phase::Complete;
    }

    // A runtime-owned registration retains this implementation until an owner
    // callback can retire it. This lets a dropped session outlive its wrapper
    // without an arbitrary thread touching Vulkan or a raw runtime pointer.
    void abandon_from_any_thread() noexcept {
        {
            std::scoped_lock lock(mutex);
            if (taken || resident_released || retirement_scheduled) {
                return;
            }
            abandoned = true;
            retirement_scheduled = true;
        }
        owner_cleanup.request();
    }

    void retire_abandoned_resident_on_owner_thread(vulkan::GpuOwnerContext& owner) {
        owner.require_owner_thread("glTF abandoned resident disposal requires the GPU owner");
        std::optional<vulkan::GpuUploadStepTicket> ticket;
        {
            std::scoped_lock lock(mutex);
            if (taken || resident_released) {
                return;
            }
            abandoned = true;
            retirement_scheduled = true;
            ticket = final_ticket;
        }
        if (ticket.has_value() && !ticket->complete() && !ticket->failed()) {
            const std::shared_ptr<Impl> self = shared_from_this();
            owner.defer_destruction_after(ticket->submission_ticket(), [self] {
                self->release_abandoned_resident_on_owner_thread();
            });
            return;
        }
        release_abandoned_resident_on_owner_thread();
    }

    void release_abandoned_resident_on_owner_thread() {
        GltfSceneResident released_resident;
        std::vector<render::Texture2D> released_default_textures;
        std::optional<TextureTask> released_texture_task;
        std::optional<MeshTask> released_mesh_task;
        std::optional<DeformationTask> released_deformation_task;
        {
            std::scoped_lock lock(mutex);
            if (taken || resident_released) {
                return;
            }
            resident_released = true;
            phase = Phase::Abandoned;
            released_resident = std::move(resident);
            released_default_textures = std::move(default_textures);
            released_texture_task = std::move(texture_task);
            released_mesh_task = std::move(mesh_task);
            released_deformation_task = std::move(deformation_task);
            resident = {};
            texture_task.reset();
            mesh_task.reset();
            deformation_task.reset();
            prepared.reset();
        }
        owner_cleanup.reset();
        // These locals deliberately destruct on the GPU owner after the lock
        // is released. They include partly created images and buffers which
        // have not yet been adopted into GltfSceneResident.
    }

    using Clock = std::chrono::steady_clock;

    [[nodiscard]] vulkan::GpuUploadStepConfig upload_step_config() const {
        return {.max_staged_byte_size = config.upload_policy.step_byte_cap,
                .owner_cpu_target_milliseconds =
                    config.upload_policy.owner_cpu_target_milliseconds};
    }

    [[nodiscard]] static double elapsed_milliseconds(Clock::time_point started) {
        return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }

    std::mutex mutex{};
    std::shared_ptr<const GltfPreparedScene> prepared{};
    GltfSceneImportConfig config{};
    vulkan::GpuRuntimeOwnerCleanup owner_cleanup{};
    GltfSceneResident resident{};
    GltfSceneUploadSessionMetrics metrics{};
    Phase phase = Phase::DefaultTextures;
    std::string failure{};
    std::size_t default_texture_index = 0;
    std::vector<render::Texture2D> default_textures{};
    std::size_t prepared_texture_index = 0;
    std::optional<TextureTask> texture_task{};
    std::size_t material_index = 0;
    std::size_t mesh_index = 0;
    std::size_t primitive_index = 0;
    std::optional<MeshTask> mesh_task{};
    bool deformation_initialized = false;
    std::size_t deformation_index = 0;
    std::optional<DeformationTask> deformation_task{};
    std::optional<vulkan::GpuUploadStepTicket> final_ticket{};
    bool backpressure_pending = false;
    std::optional<Clock::time_point> first_submission_started{};
    std::size_t pool_initial_block_count = 0;
    struct QueuedOwnerStep {
        std::optional<vulkan::GpuJobHandle<void>> job{};
    };
    std::shared_ptr<QueuedOwnerStep> queued_owner_step{};
    std::atomic<bool> owner_step_in_flight{false};
    bool taken = false;
    bool abandoned = false;
    bool retirement_scheduled = false;
    bool resident_released = false;
};

GltfSceneUploadSession::GltfSceneUploadSession(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

GltfSceneUploadSession::~GltfSceneUploadSession() {
    if (impl_ != nullptr) {
        impl_->abandon_from_any_thread();
    }
}

std::shared_ptr<GltfSceneUploadSession>
begin_gltf_scene_upload_session(std::shared_ptr<const GltfPreparedScene> prepared,
                                GltfSceneImportConfig config, vulkan::GpuRuntime& gpu) {
    const std::shared_ptr<GltfSceneUploadSession::Impl> impl =
        std::make_shared<GltfSceneUploadSession::Impl>(std::move(prepared), std::move(config), gpu);
    impl->arm_owner_cleanup(gpu);
    return std::shared_ptr<GltfSceneUploadSession>(new GltfSceneUploadSession(impl));
}

bool GltfSceneUploadSession::poll(vulkan::GpuRuntime& gpu, bool wait) {
    if (impl_ == nullptr) {
        throw std::runtime_error("glTF upload session has no implementation");
    }
    if (!impl_->owner_cleanup.belongs_to(gpu)) {
        throw std::runtime_error("glTF upload session must use its creating GPU runtime");
    }
    // The GPU owner holds Impl::mutex while it creates/records/submits one
    // indivisible step. A windowed caller must never wait behind that work in
    // host.update: it can observe this acquire-load and retry next frame.
    if (!wait && impl_->owner_step_in_flight.load(std::memory_order_acquire)) {
        return false;
    }
    try {
        for (;;) {
            std::unique_lock lock(impl_->mutex);
            if (impl_->phase == Impl::Phase::Failed) {
                // The session destructor queues owner-only deferred disposal.
                // A windowed poll reports the failure immediately; only a
                // headless finish waits for the final submitted ticket first.
                const std::string failure = impl_->failure;
                const std::optional<vulkan::GpuUploadStepTicket> final_ticket = impl_->final_ticket;
                lock.unlock();
                if (wait && final_ticket.has_value()) {
                    final_ticket->wait(gpu);
                }
                throw std::runtime_error(failure);
            }
            if (impl_->phase == Impl::Phase::Abandoned) {
                throw std::runtime_error("glTF upload session was abandoned");
            }
            if (impl_->phase == Impl::Phase::Complete) {
                return true;
            }
            if (impl_->queued_owner_step != nullptr) {
                // Retain a stable shared handle before dropping Impl::mutex to
                // wait. A concurrent poll may harvest/reset the session field,
                // but cannot invalidate this job object.
                const std::shared_ptr<Impl::QueuedOwnerStep> queued = impl_->queued_owner_step;
                if (!queued->job.has_value()) {
                    throw std::runtime_error("glTF upload session owner job is invalid");
                }
                if (!queued->job->ready()) {
                    if (!wait) {
                        return false;
                    }
                    lock.unlock();
                    queued->job->wait();
                    continue;
                }
                try {
                    queued->job->get();
                } catch (const std::exception& error) {
                    impl_->fail(error.what());
                } catch (...) {
                    impl_->fail("unknown glTF owner-step failure");
                }
                if (impl_->queued_owner_step == queued) {
                    impl_->queued_owner_step.reset();
                }
                const bool backpressured = impl_->backpressure_pending;
                const std::optional<vulkan::GpuUploadStepTicket> backpressure_ticket =
                    impl_->final_ticket;
                impl_->backpressure_pending = false;
                if (backpressured) {
                    if (!wait) {
                        // The graphics queue advances on the later app frame.
                        // Consume this edge and retry on exactly the next poll.
                        return false;
                    }
                    lock.unlock();
                    if (!backpressure_ticket.has_value()) {
                        throw std::runtime_error(
                            "glTF upload session exhausted staging before its first submission");
                    }
                    backpressure_ticket->wait(gpu);
                    continue;
                }
                if (impl_->phase == Impl::Phase::Failed) {
                    const std::string failure = impl_->failure;
                    lock.unlock();
                    throw std::runtime_error(failure);
                }
                // A completed owner job is not an enqueue performed by this
                // poll. Continue so a non-wait poll can submit at most one
                // successor advance without waiting for the GPU owner.
                continue;
            }
            if (impl_->phase == Impl::Phase::FinishedRecording) {
                if (!impl_->final_ticket.has_value()) {
                    impl_->mark_complete();
                    return true;
                }
                vulkan::GpuUploadStepTicket ticket = impl_->final_ticket.value();
                lock.unlock();
                const bool complete = wait ? (ticket.wait(gpu), true) : ticket.poll(gpu);
                if (!complete) {
                    return false;
                }
                lock.lock();
                impl_->mark_complete();
                return true;
            }
            if (wait) {
                const std::shared_ptr<Impl> impl = impl_;
                impl_->owner_step_in_flight.store(true, std::memory_order_release);
                lock.unlock();
                try {
                    static_cast<void>(gpu.submit_and_wait({
                        .label = "advance glTF upload session",
                        .work =
                            [impl](vulkan::GpuOwnerContext& owner) {
                                impl->record_one_owner_step(owner);
                            },
                    }));
                } catch (...) {
                    impl_->owner_step_in_flight.store(false, std::memory_order_release);
                    throw;
                }
                lock.lock();
                if (impl_->backpressure_pending) {
                    const std::optional<vulkan::GpuUploadStepTicket> ticket = impl_->final_ticket;
                    impl_->backpressure_pending = false;
                    lock.unlock();
                    if (!ticket.has_value()) {
                        throw std::runtime_error(
                            "glTF upload session exhausted staging before its first submission");
                    }
                    // Headless startup has no later frame submission to
                    // advance the graphics-queue completion watermark. Retire
                    // the latest same-queue step before retrying the bounded
                    // pool.
                    ticket->wait(gpu);
                } else {
                    lock.unlock();
                }
                continue;
            }
            const std::shared_ptr<Impl> impl = impl_;
            const std::shared_ptr<Impl::QueuedOwnerStep> queued =
                std::make_shared<Impl::QueuedOwnerStep>();
            impl_->owner_step_in_flight.store(true, std::memory_order_release);
            try {
                queued->job.emplace(gpu.submit("advance glTF upload session",
                                               [impl](vulkan::GpuOwnerContext& owner) {
                                                   impl->record_one_owner_step(owner);
                                               }));
                impl_->queued_owner_step = queued;
            } catch (...) {
                impl_->owner_step_in_flight.store(false, std::memory_order_release);
                throw;
            }
            return false;
        }
    } catch (const std::exception& error) {
        impl_->record_terminal_failure(error.what());
        throw;
    } catch (...) {
        impl_->record_terminal_failure("unknown glTF upload session failure");
        throw;
    }
}

bool GltfSceneUploadSession::complete() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->phase == Impl::Phase::Complete;
}

bool GltfSceneUploadSession::failed() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->phase == Impl::Phase::Failed;
}

std::string GltfSceneUploadSession::failure_message() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->failure;
}

GltfSceneUploadSessionMetrics GltfSceneUploadSession::metrics() const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->metrics;
}

GltfSceneResident GltfSceneUploadSession::take_resident() {
    GltfSceneResident resident;
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->phase != Impl::Phase::Complete) {
            throw std::runtime_error("glTF upload session resident is not GPU complete");
        }
        if (impl_->taken) {
            throw std::runtime_error("glTF upload session resident was already taken");
        }
        impl_->taken = true;
        resident = std::move(impl_->resident);
    }
    // The resident is now owned by the caller; release the runtime's strong
    // session-retention action so the successful path cannot form a cycle.
    impl_->owner_cleanup.reset();
    return resident;
}

} // namespace cubey
