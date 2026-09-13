#include "gltf_scene_resident_builder.h"

#include <cubey/render/deformation.h>
#include <cubey/render/pbr_material_resources.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/staging_pool.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey {
namespace {

[[nodiscard]] VkDeviceSize checked_device_size(std::size_t value, const char* message) {
    if (value > std::numeric_limits<VkDeviceSize>::max()) {
        throw std::runtime_error(message);
    }
    return static_cast<VkDeviceSize>(value);
}

[[nodiscard]] VkDeviceSize checked_device_size_mul(VkDeviceSize lhs, VkDeviceSize rhs,
                                                   const char* message) {
    if (lhs != 0U && rhs > std::numeric_limits<VkDeviceSize>::max() / lhs) {
        throw std::runtime_error(message);
    }
    return lhs * rhs;
}

[[nodiscard]] VkDeviceSize checked_device_size_add(VkDeviceSize lhs, VkDeviceSize rhs,
                                                   const char* message) {
    if (lhs > std::numeric_limits<VkDeviceSize>::max() - rhs) {
        throw std::runtime_error(message);
    }
    return lhs + rhs;
}

[[nodiscard]] std::size_t checked_size_t(VkDeviceSize value, const char* message) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error(message);
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::uint32_t checked_u32_size(std::size_t value, const char* message) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(message);
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t block_count(std::uint32_t extent, std::uint32_t block_extent,
                                        const char* message) {
    if (extent == 0U || block_extent == 0U) {
        throw std::runtime_error(message);
    }
    return 1U + ((extent - 1U) / block_extent);
}

template <typename T>
[[nodiscard]] T& required_state(std::optional<T>& value, const char* message) {
    if (!value.has_value()) {
        throw std::runtime_error(message);
    }
    return *value;
}

template <typename T>
[[nodiscard]] const T& required_state(const std::optional<T>& value, const char* message) {
    if (!value.has_value()) {
        throw std::runtime_error(message);
    }
    return *value;
}

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
            texture = &render::pbr_default_texture(
                required_state(resources.default_textures,
                               "glTF material upload requires default textures"),
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
        offset = checked_device_size_add(
            offset,
            checked_device_size(byte_count, "glTF texture mip byte size exceeds device range"),
            "glTF texture mip offsets overflow");
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

template <typename T>
[[nodiscard]] VkDeviceSize span_byte_size(std::span<const T> values, const char* message) {
    if (values.empty()) {
        throw std::runtime_error(message);
    }
    const VkDeviceSize byte_count =
        checked_device_size_mul(checked_device_size(values.size(), message), sizeof(T), message);
    static_cast<void>(checked_size_t(byte_count, message));
    return byte_count;
}

template <typename T>
[[nodiscard]] vulkan::Buffer create_host_storage_buffer(const vulkan::Device& device,
                                                        std::span<const T> initial_values) {
    const VkDeviceSize byte_count = span_byte_size(
        initial_values, "glTF deformation host storage buffer byte size must be positive");
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

[[nodiscard]] bool deformation_has_morph(GltfPrimitiveDeformationKind kind) {
    return kind == GltfPrimitiveDeformationKind::Morph ||
           kind == GltfPrimitiveDeformationKind::MorphSkin;
}

void validate_prepared_texture(const GltfPreparedTexture& texture) {
    if (texture.extent.width == 0U || texture.extent.height == 0U || texture.mip_levels == 0U ||
        texture.format == VK_FORMAT_UNDEFINED || texture.bytes.empty()) {
        throw std::runtime_error("prepared glTF texture metadata is incomplete");
    }
    if (texture.mip_levels > render::texture_2d_mip_count(texture.extent)) {
        throw std::runtime_error("prepared glTF texture mip count exceeds its extent");
    }
    const std::vector<render::UploadedTexture2DMip> mips = texture_mips(texture);
    if (mips.size() != texture.mip_levels) {
        throw std::runtime_error("prepared glTF texture mip count is inconsistent");
    }
    const VkDeviceSize source_size = checked_device_size(
        texture.bytes.size(), "prepared glTF texture byte size exceeds device range");
    for (std::uint32_t mip_index = 0; mip_index < texture.mip_levels; ++mip_index) {
        const render::UploadedTexture2DMip& mip = mips[mip_index];
        const VkExtent2D expected_extent = render::texture_2d_mip_extent(texture.extent, mip_index);
        if (mip.extent.width != expected_extent.width ||
            mip.extent.height != expected_extent.height) {
            throw std::runtime_error("prepared glTF texture mip extent is inconsistent");
        }
        const std::size_t expected_byte_count =
            render::texture_2d_byte_size(expected_extent, 1U, texture.format);
        if (mip.byte_count != expected_byte_count) {
            throw std::runtime_error("prepared glTF texture mip byte size is inconsistent");
        }
        const VkDeviceSize byte_count = checked_device_size(
            mip.byte_count, "prepared glTF texture mip byte size exceeds device range");
        if (mip.byte_offset > source_size || byte_count > source_size - mip.byte_offset) {
            throw std::runtime_error("prepared glTF texture mip range exceeds source bytes");
        }
    }
}

void validate_prepared_deformation(const GltfPreparedScene& prepared,
                                   const GltfPreparedDeformationPrimitive& source) {
    if (!gltf_primitive_requires_deformation(source.deformation)) {
        throw std::runtime_error("prepared glTF deformation primitive must be deformable");
    }
    if (source.node_index >= prepared.nodes.size() || source.mesh_index >= prepared.meshes.size() ||
        source.primitive_index >= prepared.meshes[source.mesh_index].primitives.size()) {
        throw std::runtime_error("prepared glTF deformation primitive index is out of range");
    }
    const GltfPreparedMeshPrimitive& geometry =
        prepared.meshes[source.mesh_index].primitives[source.primitive_index];
    if (geometry.vertices.empty() || geometry.indices.empty()) {
        throw std::runtime_error("prepared glTF deformation geometry requires indexed vertices");
    }
    const std::size_t vertex_count = geometry.vertices.size();
    static_cast<void>(checked_u32_size(
        vertex_count, "prepared glTF deformation vertex count exceeds uint32 range"));
    static_cast<void>(span_byte_size(std::span{geometry.vertices},
                                     "prepared glTF deformation vertex byte size is invalid"));
    static_cast<void>(span_byte_size(std::span{geometry.indices},
                                     "prepared glTF deformation index byte size is invalid"));

    const bool has_morph = deformation_has_morph(source.deformation);
    if (has_morph) {
        if (source.morph_target_count == 0U ||
            source.initial_morph_weights.size() != source.morph_target_count) {
            throw std::runtime_error("prepared glTF deformation morph metadata is inconsistent");
        }
        const VkDeviceSize expected = checked_device_size_mul(
            checked_device_size_mul(source.morph_target_count, vertex_count,
                                    "prepared glTF deformation morph payload size overflows"),
            9U, "prepared glTF deformation morph payload size overflows");
        if (source.morph_targets.size() !=
            checked_size_t(expected,
                           "prepared glTF deformation morph payload exceeds host range")) {
            throw std::runtime_error(
                "prepared glTF deformation morph payload size is inconsistent");
        }
    } else if (source.morph_target_count != 0U || source.morph_targets.size() != 1U ||
               source.initial_morph_weights.size() != 1U) {
        throw std::runtime_error("prepared glTF deformation morph sentinel is inconsistent");
    }

    const bool has_skin = deformation_has_skin(source.deformation);
    if (has_skin) {
        if (source.skin_index == asset::kInvalidAssetIndex || source.joint_count == 0U ||
            source.skin_influences.size() != vertex_count ||
            source.initial_joint_palette.size() != source.joint_count) {
            throw std::runtime_error("prepared glTF deformation skin metadata is inconsistent");
        }
        for (const GltfSkinInfluence& influence : source.skin_influences) {
            const float weight_sum = influence.weights.x + influence.weights.y +
                                     influence.weights.z + influence.weights.w;
            if (weight_sum <= 1.0e-6F) {
                throw std::runtime_error("prepared glTF deformation skin weights are invalid");
            }
            for (std::size_t component = 0; component < influence.joints.size(); ++component) {
                const float weight = influence.weights[static_cast<int>(component)];
                if (weight < 0.0F) {
                    throw std::runtime_error("prepared glTF deformation skin weights are invalid");
                }
                if (weight > 0.0F && influence.joints[component] >= source.joint_count) {
                    throw std::runtime_error(
                        "prepared glTF deformation skin joint index is out of range");
                }
            }
        }
    } else if (source.skin_index != asset::kInvalidAssetIndex || source.joint_count != 0U ||
               source.skin_influences.size() != 1U || source.initial_joint_palette.size() != 1U) {
        throw std::runtime_error("prepared glTF deformation skin sentinel is inconsistent");
    }

    static_cast<void>(span_byte_size(std::span{source.morph_targets},
                                     "prepared glTF deformation morph byte size is invalid"));
    static_cast<void>(span_byte_size(std::span{source.skin_influences},
                                     "prepared glTF deformation skin byte size is invalid"));
    static_cast<void>(span_byte_size(std::span{source.initial_morph_weights},
                                     "prepared glTF deformation morph weights are invalid"));
    static_cast<void>(span_byte_size(std::span{source.initial_joint_palette},
                                     "prepared glTF deformation joint palette is invalid"));
}

void validate_prepared_scene(const GltfPreparedScene& prepared) {
    for (const GltfPreparedTexture& texture : prepared.textures) {
        validate_prepared_texture(texture);
    }
    for (const GltfPreparedMesh& mesh : prepared.meshes) {
        for (const GltfPreparedMeshPrimitive& primitive : mesh.primitives) {
            if (primitive.vertices.empty() || primitive.indices.empty()) {
                throw std::runtime_error("prepared glTF mesh primitive requires indexed vertices");
            }
            if (primitive.material_index >= prepared.materials.size()) {
                throw std::runtime_error(
                    "prepared glTF mesh primitive material index is out of range");
            }
            static_cast<void>(span_byte_size(std::span{primitive.vertices},
                                             "prepared glTF vertex byte size is invalid"));
            static_cast<void>(span_byte_size(std::span{primitive.indices},
                                             "prepared glTF index byte size is invalid"));
            static_cast<void>(checked_u32_size(primitive.vertices.size(),
                                               "prepared glTF vertex count exceeds uint32 range"));
            static_cast<void>(checked_u32_size(primitive.indices.size(),
                                               "prepared glTF index count exceeds uint32 range"));
            for (const std::uint32_t index : primitive.indices) {
                if (index >= primitive.vertices.size()) {
                    throw std::runtime_error("prepared glTF mesh index is out of range");
                }
            }
        }
    }
    for (const GltfPreparedDeformationPrimitive& source : prepared.deformable_primitives) {
        validate_prepared_deformation(prepared, source);
    }
}

} // namespace

struct GltfSceneResidentBuilder::Impl {
    enum class Phase {
        DefaultTextures,
        Textures,
        Materials,
        Meshes,
        Deformation,
        FinishedRecording,
        Complete,
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

    enum class OperationResult {
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
            const VkDeviceSize byte_count = checked_device_size(
                bytes.size(), "glTF upload staging source size exceeds device range");
            if (staged_byte_count_ > config_.max_staged_byte_size ||
                byte_count > config_.max_staged_byte_size - staged_byte_count_) {
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
            return {.result = StagingAttempt::Result::Staged,
                    .slice = required_state(slice, "glTF upload staging slice is missing")};
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
                  GltfSceneImportConfig config_value)
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
        validate_prepared_scene(*prepared);
        metrics.owner_target_milliseconds = config.upload_policy.owner_cpu_target_milliseconds;
        metrics.step_byte_cap = config.upload_policy.step_byte_cap;
        metrics.copy_byte_target = config.upload_policy.copy_byte_target;
        resident.resources.mesh_primitives.resize(prepared->meshes.size());
    }

    [[nodiscard]] GltfSceneResidentBuilder::AdvanceResult advance(vulkan::GpuOwnerContext& owner) {
        owner.require_owner_thread("glTF upload session requires the GPU owner thread");
        const Clock::time_point started = Clock::now();
        GltfSceneResidentBuilder::AdvanceResult outcome{};
        try {
            OwnerStepAccumulator step(owner, upload_step_config());
            for (;;) {
                const OperationResult result = advance_one_owner_operation(owner, step);
                outcome.made_progress =
                    outcome.made_progress || result == OperationResult::Progress;
                if (result == OperationResult::Backpressure) {
                    ++metrics.backpressure_count;
                    outcome.backpressured = true;
                    break;
                }
                if (result == OperationResult::StepFull || phase == Phase::FinishedRecording ||
                    elapsed_milliseconds(started) >= metrics.owner_target_milliseconds) {
                    break;
                }
            }
            if (step.has_copies()) {
                vulkan::GpuUploadStepTicket ticket = step.submit();
                record_submitted_step(owner, ticket);
                outcome.submitted_ticket = std::move(ticket);
                if (step.has_static_mesh_copy()) {
                    ++resident.mesh_upload_transfer_submission_count;
                }
            }
        } catch (...) {
            record_owner_advance_timing(started);
            throw;
        }
        record_owner_advance_timing(started);
        outcome.finished_recording = phase == Phase::FinishedRecording;
        return outcome;
    }

    [[nodiscard]] OperationResult advance_one_owner_operation(vulkan::GpuOwnerContext& owner,
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
            return OperationResult::Progress;
        }
        return OperationResult::Progress;
    }

    [[nodiscard]] OperationResult record_default_texture(vulkan::GpuOwnerContext& owner,
                                                         OwnerStepAccumulator& step) {
        const std::span<const render::PbrDefaultTextureSpec> specs =
            render::pbr_default_texture_specs();
        if (default_texture_index >= specs.size()) {
            resident.resources.default_textures.emplace(
                render::make_pbr_default_texture_set(std::move(default_textures)));
            phase = Phase::Textures;
            return OperationResult::Progress;
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
        const OperationResult result = record_texture_copy(
            owner, step,
            required_state(texture_task, "glTF default texture upload task is missing"),
            texture_finished);
        if (result != OperationResult::Progress) {
            return result;
        }
        if (texture_finished) {
            default_textures.push_back(std::move(required_state(
                texture_task->texture, "glTF default texture upload did not create a texture")));
            texture_task.reset();
            ++default_texture_index;
        }
        return OperationResult::Progress;
    }

    [[nodiscard]] OperationResult record_prepared_texture(vulkan::GpuOwnerContext& owner,
                                                          OwnerStepAccumulator& step) {
        if (prepared_texture_index >= prepared->textures.size()) {
            phase = Phase::Materials;
            return OperationResult::Progress;
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
        const OperationResult result = record_texture_copy(
            owner, step,
            required_state(texture_task, "glTF prepared texture upload task is missing"),
            texture_finished);
        if (result != OperationResult::Progress) {
            return result;
        }
        if (texture_finished) {
            resident.resources.textures.push_back(std::move(required_state(
                texture_task->texture, "glTF prepared texture upload did not create a texture")));
            texture_task.reset();
            ++prepared_texture_index;
        }
        return OperationResult::Progress;
    }

    [[nodiscard]] OperationResult record_texture_copy(vulkan::GpuOwnerContext& owner,
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
            return OperationResult::Progress;
        }
        if (task.mip_index >= task.mips.size()) {
            texture_finished = true;
            return OperationResult::Progress;
        }
        const render::UploadedTexture2DMip& mip = task.mips[task.mip_index];
        const render::TextureFormatLayout layout = render::texture_format_layout(task.format);
        const std::uint32_t blocks_x = block_count(mip.extent.width, layout.block_width,
                                                   "glTF texture upload block width is invalid");
        const std::uint32_t blocks_y = block_count(mip.extent.height, layout.block_height,
                                                   "glTF texture upload block height is invalid");
        const VkDeviceSize bytes_per_row = checked_device_size_mul(
            blocks_x,
            checked_device_size(layout.bytes_per_block,
                                "glTF texture upload block byte size exceeds device range"),
            "glTF texture upload row size overflows");
        if (bytes_per_row == 0U) {
            throw std::runtime_error("glTF texture upload row size is zero");
        }
        if (task.block_row > blocks_y) {
            throw std::runtime_error("glTF texture upload block row is out of range");
        }
        const std::uint32_t remaining_rows = blocks_y - task.block_row;
        if (remaining_rows == 0U) {
            throw std::runtime_error("glTF texture upload selected a completed mip");
        }
        const VkDeviceSize target_rows =
            std::max<VkDeviceSize>(1U, config.upload_policy.copy_byte_target / bytes_per_row);
        const std::uint32_t row_count =
            static_cast<std::uint32_t>(std::min<VkDeviceSize>(remaining_rows, target_rows));
        const VkDeviceSize byte_count = checked_device_size_mul(
            bytes_per_row, row_count, "glTF texture upload chunk size overflows");
        const VkDeviceSize byte_offset = checked_device_size_add(
            mip.byte_offset,
            checked_device_size_mul(bytes_per_row, task.block_row,
                                    "glTF texture upload row offset overflows"),
            "glTF texture upload byte offset overflows");
        const std::size_t source_offset =
            checked_size_t(byte_offset, "glTF texture upload byte offset exceeds host range");
        const std::size_t source_byte_count =
            checked_size_t(byte_count, "glTF texture upload chunk size exceeds host range");
        if (source_offset > task.source.size() ||
            source_byte_count > task.source.size() - source_offset) {
            throw std::runtime_error("glTF texture chunk exceeds prepared bytes");
        }

        const StagingAttempt attempt = step.try_stage(
            {reinterpret_cast<const std::byte*>(task.source.data() + source_offset),
             source_byte_count},
            std::max<VkDeviceSize>(
                4U,
                checked_device_size(layout.bytes_per_block,
                                    "glTF texture upload block alignment exceeds device range")));
        if (attempt.result == StagingAttempt::Result::StepFull) {
            return OperationResult::StepFull;
        }
        if (attempt.result == StagingAttempt::Result::Backpressure) {
            return OperationResult::Backpressure;
        }
        if (!task.transitioned) {
            vulkan::transition_image_layout(
                step.command_buffer(),
                vulkan::begin_transfer_dst_transition(task.texture->handle(), task.mip_levels, 1));
            task.transitioned = true;
        }
        const std::uint32_t image_y = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(task.block_row) * layout.block_height);
        const std::uint32_t remaining_height = mip.extent.height - image_y;
        const std::uint32_t copy_height =
            row_count == remaining_rows
                ? remaining_height
                : static_cast<std::uint32_t>(static_cast<std::uint64_t>(row_count) *
                                             layout.block_height);
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
        return OperationResult::Progress;
    }

    [[nodiscard]] OperationResult record_material(vulkan::GpuOwnerContext& owner) {
        if (material_index >= prepared->materials.size()) {
            phase = Phase::Meshes;
            return OperationResult::Progress;
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
        return OperationResult::Progress;
    }

    [[nodiscard]] OperationResult record_mesh(vulkan::GpuOwnerContext& owner,
                                              OwnerStepAccumulator& step) {
        while (mesh_index < prepared->meshes.size() &&
               primitive_index >= prepared->meshes[mesh_index].primitives.size()) {
            ++mesh_index;
            primitive_index = 0;
        }
        if (mesh_index >= prepared->meshes.size()) {
            initialize_deformation_primitives();
            phase = Phase::Deformation;
            return OperationResult::Progress;
        }
        const GltfPreparedMeshPrimitive& source =
            prepared->meshes[mesh_index].primitives[primitive_index];
        if (!mesh_task.has_value()) {
            mesh_task.emplace(MeshTask{.source = &source});
        }
        MeshTask& task = required_state(mesh_task, "glTF mesh upload task is missing");
        const VkDeviceSize vertex_bytes = span_byte_size(
            std::span{task.source->vertices}, "glTF mesh vertex buffer byte size is invalid");
        const VkDeviceSize index_bytes = span_byte_size(
            std::span{task.source->indices}, "glTF mesh index buffer byte size is invalid");
        if (!task.vertex.has_value()) {
            task.vertex.emplace(owner.device(),
                                vulkan::device_local_buffer_config(
                                    vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            return OperationResult::Progress;
        }
        if (!task.index.has_value()) {
            task.index.emplace(owner.device(), vulkan::device_local_buffer_config(
                                                   index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
            return OperationResult::Progress;
        }
        if (task.vertex_offset < vertex_bytes) {
            return record_buffer_copy(
                step,
                std::span<const std::byte>{
                    reinterpret_cast<const std::byte*>(task.source->vertices.data()),
                    static_cast<std::size_t>(vertex_bytes)},
                required_state(task.vertex, "glTF mesh vertex buffer is missing"),
                task.vertex_offset, true);
        }
        if (task.index_offset < index_bytes) {
            return record_buffer_copy(
                step,
                std::span<const std::byte>{
                    reinterpret_cast<const std::byte*>(task.source->indices.data()),
                    static_cast<std::size_t>(index_bytes)},
                required_state(task.index, "glTF mesh index buffer is missing"), task.index_offset,
                true);
        }
        const render::MeshHandle handle = staging_mesh_handle(resident.static_mesh_handles.size());
        resident.resources.meshes.emplace(
            handle,
            render::Mesh::from_uploaded_buffers(
                std::move(required_state(task.vertex, "glTF mesh vertex buffer is missing")),
                std::move(required_state(task.index, "glTF mesh index buffer is missing")),
                VK_INDEX_TYPE_UINT32,
                checked_u32_size(task.source->indices.size(),
                                 "glTF mesh index count exceeds uint32 range")));
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
        return OperationResult::Progress;
    }

    [[nodiscard]] OperationResult record_buffer_copy(OwnerStepAccumulator& step,
                                                     std::span<const std::byte> source,
                                                     vulkan::Buffer& destination,
                                                     VkDeviceSize& offset,
                                                     bool count_static_mesh_bytes) {
        const VkDeviceSize source_size =
            checked_device_size(source.size(), "glTF buffer copy source size exceeds device range");
        if (offset > source_size) {
            throw std::runtime_error("glTF buffer copy offset exceeds prepared source");
        }
        const VkDeviceSize remaining = source_size - offset;
        if (remaining == 0U) {
            throw std::runtime_error("glTF buffer copy selected an exhausted source");
        }
        const VkDeviceSize byte_count = std::min(remaining, config.upload_policy.copy_byte_target);
        const StagingAttempt attempt = step.try_stage(
            source.subspan(
                checked_size_t(offset, "glTF buffer copy offset exceeds host range"),
                checked_size_t(byte_count, "glTF buffer copy chunk size exceeds host range")),
            4U);
        if (attempt.result == StagingAttempt::Result::StepFull) {
            return OperationResult::StepFull;
        }
        if (attempt.result == StagingAttempt::Result::Backpressure) {
            return OperationResult::Backpressure;
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
        return OperationResult::Progress;
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

    [[nodiscard]] OperationResult record_deformation(vulkan::GpuOwnerContext& owner,
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
                return OperationResult::Progress;
            }
            phase = Phase::FinishedRecording;
            return OperationResult::Progress;
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
                .vertex_count = checked_u32_size(
                    geometry.vertices.size(), "glTF deformation vertex count exceeds uint32 range"),
                .morph_target_count = source.morph_target_count,
                .joint_count = source.joint_count,
                .flags = deformation_flags(source.deformation),
            };
            deformation_task.emplace(DeformationTask{
                .source = &source,
                .geometry = &geometry,
                .resource_index = resident.resources.deformation.primitives.size() - 1U,
            });
        }
        return record_deformation_task(
            owner, step,
            required_state(deformation_task, "glTF deformation upload task is missing"));
    }

    [[nodiscard]] OperationResult record_deformation_task(vulkan::GpuOwnerContext& owner,
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
                vulkan::device_local_buffer_config(
                    span_byte_size(std::span{geometry.vertices},
                                   "glTF deformation base vertex byte size is invalid"),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadBaseVertices;
            return OperationResult::Progress;
        case DeformationTask::Stage::UploadBaseVertices: {
            const VkDeviceSize total = span_byte_size(
                std::span{geometry.vertices}, "glTF deformation base vertex byte size is invalid");
            const OperationResult result =
                upload({reinterpret_cast<const std::byte*>(geometry.vertices.data()),
                        static_cast<std::size_t>(total)},
                       required_state(resource.base_vertices,
                                      "glTF deformation base vertex buffer is missing"),
                       task.base_vertex_offset);
            if (result == OperationResult::Progress && task.base_vertex_offset == total) {
                task.stage = DeformationTask::Stage::CreateMorphTargets;
            }
            return result;
        }
        case DeformationTask::Stage::CreateMorphTargets:
            resource.morph_targets.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(
                    span_byte_size(std::span{source.morph_targets},
                                   "glTF deformation morph target byte size is invalid"),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadMorphTargets;
            return OperationResult::Progress;
        case DeformationTask::Stage::UploadMorphTargets: {
            const VkDeviceSize total =
                span_byte_size(std::span{source.morph_targets},
                               "glTF deformation morph target byte size is invalid");
            const OperationResult result =
                upload({reinterpret_cast<const std::byte*>(source.morph_targets.data()),
                        static_cast<std::size_t>(total)},
                       required_state(resource.morph_targets,
                                      "glTF deformation morph target buffer is missing"),
                       task.morph_target_offset);
            if (result == OperationResult::Progress && task.morph_target_offset == total) {
                task.stage = DeformationTask::Stage::CreateSkinInfluences;
            }
            return result;
        }
        case DeformationTask::Stage::CreateSkinInfluences:
            resource.skin_influences.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(
                    span_byte_size(std::span{source.skin_influences},
                                   "glTF deformation skin influence byte size is invalid"),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadSkinInfluences;
            return OperationResult::Progress;
        case DeformationTask::Stage::UploadSkinInfluences: {
            const VkDeviceSize total =
                span_byte_size(std::span{source.skin_influences},
                               "glTF deformation skin influence byte size is invalid");
            const OperationResult result =
                upload({reinterpret_cast<const std::byte*>(source.skin_influences.data()),
                        static_cast<std::size_t>(total)},
                       required_state(resource.skin_influences,
                                      "glTF deformation skin influence buffer is missing"),
                       task.skin_influence_offset);
            if (result == OperationResult::Progress && task.skin_influence_offset == total) {
                task.stage = DeformationTask::Stage::CreateFrameStorage;
            }
            return result;
        }
        case DeformationTask::Stage::CreateFrameStorage:
            if (task.frame_index >= config.frame_slot_count) {
                task.frame_index = 0;
                task.stage = DeformationTask::Stage::CreateOutputVertex;
                return OperationResult::Progress;
            }
            resource.morph_weights.push_back(create_host_storage_buffer(
                owner.device(), std::span<const float>{source.initial_morph_weights}));
            resource.joint_palettes.push_back(create_host_storage_buffer(
                owner.device(), std::span<const math::Mat4>{source.initial_joint_palette}));
            ++task.frame_index;
            return OperationResult::Progress;
        case DeformationTask::Stage::CreateOutputVertex:
            if (task.frame_index >= config.frame_slot_count) {
                task.stage = DeformationTask::Stage::CreateDescriptors;
                return OperationResult::Progress;
            }
            task.output_vertex.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(
                    span_byte_size(std::span{geometry.vertices},
                                   "glTF deformation output vertex byte size is invalid"),
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadOutputVertex;
            return OperationResult::Progress;
        case DeformationTask::Stage::UploadOutputVertex: {
            const VkDeviceSize total =
                span_byte_size(std::span{geometry.vertices},
                               "glTF deformation output vertex byte size is invalid");
            const OperationResult result =
                upload({reinterpret_cast<const std::byte*>(geometry.vertices.data()),
                        static_cast<std::size_t>(total)},
                       required_state(task.output_vertex,
                                      "glTF deformation output vertex buffer is missing"),
                       task.output_vertex_offset);
            if (result == OperationResult::Progress && task.output_vertex_offset == total) {
                task.stage = DeformationTask::Stage::CreateOutputIndex;
            }
            return result;
        }
        case DeformationTask::Stage::CreateOutputIndex:
            task.output_index.emplace(
                owner.device(),
                vulkan::device_local_buffer_config(
                    span_byte_size(std::span{geometry.indices},
                                   "glTF deformation output index byte size is invalid"),
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
            task.stage = DeformationTask::Stage::UploadOutputIndex;
            return OperationResult::Progress;
        case DeformationTask::Stage::UploadOutputIndex: {
            const VkDeviceSize total = span_byte_size(
                std::span{geometry.indices}, "glTF deformation output index byte size is invalid");
            const OperationResult result =
                upload({reinterpret_cast<const std::byte*>(geometry.indices.data()),
                        static_cast<std::size_t>(total)},
                       required_state(task.output_index,
                                      "glTF deformation output index buffer is missing"),
                       task.output_index_offset);
            if (result == OperationResult::Progress && task.output_index_offset == total) {
                task.stage = DeformationTask::Stage::FinishOutput;
            }
            return result;
        }
        case DeformationTask::Stage::FinishOutput:
            resource.output_meshes.push_back(render::Mesh::from_uploaded_buffers(
                std::move(required_state(task.output_vertex,
                                         "glTF deformation output vertex buffer is missing")),
                std::move(required_state(task.output_index,
                                         "glTF deformation output index buffer is missing")),
                VK_INDEX_TYPE_UINT32,
                checked_u32_size(geometry.indices.size(),
                                 "glTF deformation index count exceeds uint32 range")));
            task.output_vertex.reset();
            task.output_index.reset();
            task.output_vertex_offset = 0;
            task.output_index_offset = 0;
            ++task.frame_index;
            task.stage = DeformationTask::Stage::CreateOutputVertex;
            return OperationResult::Progress;
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
            return OperationResult::Progress;
        }
        throw std::runtime_error("unknown glTF deformation upload stage");
    }

    void record_submitted_step(vulkan::GpuOwnerContext& owner,
                               const vulkan::GpuUploadStepTicket& ticket) {
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
        resident.final_upload_step = ticket;
    }

    void mark_complete() {
        if (phase == Phase::Complete) {
            return;
        }
        if (first_submission_started.has_value()) {
            metrics.first_step_to_final_completion_milliseconds =
                elapsed_milliseconds(required_state(
                    first_submission_started, "glTF upload completion start time is missing"));
        }
        resident.upload_session_metrics = metrics;
        phase = Phase::Complete;
    }

    [[nodiscard]] GltfSceneUploadSessionMetrics metrics_snapshot() const {
        return metrics;
    }

    [[nodiscard]] GltfSceneResident take_resident() {
        return std::move(resident);
    }

    using Clock = std::chrono::steady_clock;

    [[nodiscard]] vulkan::GpuUploadStepConfig upload_step_config() const {
        return {.max_staged_byte_size = config.upload_policy.step_byte_cap,
                .owner_cpu_target_milliseconds =
                    config.upload_policy.owner_cpu_target_milliseconds};
    }

    void record_owner_advance_timing(Clock::time_point started) {
        const double elapsed = elapsed_milliseconds(started);
        ++metrics.owner_advance_count;
        metrics.owner_total_milliseconds += elapsed;
        metrics.owner_max_step_milliseconds =
            std::max(metrics.owner_max_step_milliseconds, elapsed);
        if (elapsed > metrics.owner_target_milliseconds) {
            ++metrics.owner_over_target_step_count;
        }
        resident.upload_session_metrics = metrics;
    }

    [[nodiscard]] static double elapsed_milliseconds(Clock::time_point started) {
        return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    }

    std::shared_ptr<const GltfPreparedScene> prepared{};
    GltfSceneImportConfig config{};
    GltfSceneResident resident{};
    GltfSceneUploadSessionMetrics metrics{};
    Phase phase = Phase::DefaultTextures;
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
    std::optional<Clock::time_point> first_submission_started{};
    std::size_t pool_initial_block_count = 0;
};

GltfSceneResidentBuilder::GltfSceneResidentBuilder(
    std::shared_ptr<const GltfPreparedScene> prepared, GltfSceneImportConfig config)
    : impl_(std::make_unique<Impl>(std::move(prepared), std::move(config))) {}

GltfSceneResidentBuilder::~GltfSceneResidentBuilder() = default;

GltfSceneResidentBuilder::AdvanceResult
GltfSceneResidentBuilder::advance(vulkan::GpuOwnerContext& owner) {
    return impl_->advance(owner);
}

void GltfSceneResidentBuilder::mark_complete() {
    impl_->mark_complete();
}

GltfSceneUploadSessionMetrics GltfSceneResidentBuilder::metrics() const {
    return impl_->metrics_snapshot();
}

GltfSceneResident GltfSceneResidentBuilder::take_resident() {
    return impl_->take_resident();
}

} // namespace cubey
