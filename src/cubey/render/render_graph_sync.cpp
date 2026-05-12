#include <cubey/render/render_graph.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace cubey::render {
namespace {

[[nodiscard]] bool is_depth_aspect(VkImageAspectFlags aspects) {
    return aspects == VK_IMAGE_ASPECT_DEPTH_BIT;
}

[[nodiscard]] bool is_texture_read(RenderGraphTextureUsage usage) {
    return usage == RenderGraphTextureUsage::SampledRead ||
           usage == RenderGraphTextureUsage::StorageRead ||
           usage == RenderGraphTextureUsage::StorageReadWrite ||
           usage == RenderGraphTextureUsage::TransferRead;
}

[[nodiscard]] bool is_texture_write(RenderGraphTextureUsage usage) {
    return usage == RenderGraphTextureUsage::StorageWrite ||
           usage == RenderGraphTextureUsage::StorageReadWrite ||
           usage == RenderGraphTextureUsage::ColorAttachment ||
           usage == RenderGraphTextureUsage::DepthAttachment ||
           usage == RenderGraphTextureUsage::TransferWrite;
}

[[nodiscard]] bool is_buffer_read(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::UniformRead ||
           usage == RenderGraphBufferUsage::StorageRead ||
           usage == RenderGraphBufferUsage::StorageReadWrite ||
           usage == RenderGraphBufferUsage::TransferRead;
}

[[nodiscard]] bool is_buffer_write(RenderGraphBufferUsage usage) {
    return usage == RenderGraphBufferUsage::StorageWrite ||
           usage == RenderGraphBufferUsage::StorageReadWrite ||
           usage == RenderGraphBufferUsage::TransferWrite;
}

[[nodiscard]] VkPipelineStageFlags shader_stage_for_pass(RenderGraphQueueDomain domain) {
    if (domain == RenderGraphQueueDomain::Compute) {
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
}

[[nodiscard]] RenderGraphTextureState
texture_usage_state(const RenderGraphCompiledPass& pass, const RenderGraphTextureResource& resource,
                    RenderGraphTextureUsage usage) {
    switch (usage) {
    case RenderGraphTextureUsage::SampledRead:
        return {
            .layout = is_depth_aspect(resource.desc.aspects)
                          ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphTextureUsage::StorageRead:
        return {
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphTextureUsage::StorageWrite:
        return {
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .access_mask = VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphTextureUsage::StorageReadWrite:
        return {
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphTextureUsage::ColorAttachment:
        return {
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        };
    case RenderGraphTextureUsage::DepthAttachment:
        return {
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        };
    case RenderGraphTextureUsage::TransferRead:
        return {
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .access_mask = VK_ACCESS_TRANSFER_READ_BIT,
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        };
    case RenderGraphTextureUsage::TransferWrite:
        return {
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        };
    }
    throw std::runtime_error("render graph texture usage is invalid");
}

[[nodiscard]] RenderGraphBufferState buffer_usage_state(const RenderGraphCompiledPass& pass,
                                                        RenderGraphBufferUsage usage) {
    switch (usage) {
    case RenderGraphBufferUsage::UniformRead:
        return {
            .access_mask = VK_ACCESS_UNIFORM_READ_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphBufferUsage::StorageRead:
        return {
            .access_mask = VK_ACCESS_SHADER_READ_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphBufferUsage::StorageWrite:
        return {
            .access_mask = VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphBufferUsage::StorageReadWrite:
        return {
            .access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .stage_mask = shader_stage_for_pass(pass.queue_domain),
        };
    case RenderGraphBufferUsage::TransferRead:
        return {
            .access_mask = VK_ACCESS_TRANSFER_READ_BIT,
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        };
    case RenderGraphBufferUsage::TransferWrite:
        return {
            .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        };
    }
    throw std::runtime_error("render graph buffer usage is invalid");
}

struct LastTextureAccess {
    bool valid = false;
    std::size_t pass_index = 0;
    RenderGraphTextureUsage usage = RenderGraphTextureUsage::SampledRead;
};

struct LastBufferAccess {
    bool valid = false;
    std::size_t pass_index = 0;
    RenderGraphBufferUsage usage = RenderGraphBufferUsage::UniformRead;
};

[[nodiscard]] bool needs_texture_barrier(RenderGraphTextureUsage previous,
                                         RenderGraphTextureUsage next) {
    return is_texture_write(previous) || is_texture_write(next);
}

[[nodiscard]] bool needs_buffer_barrier(RenderGraphBufferUsage previous,
                                        RenderGraphBufferUsage next) {
    return is_buffer_write(previous) || is_buffer_write(next);
}

[[nodiscard]] RenderGraphTextureState transient_initial_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
}

[[nodiscard]] RenderGraphTextureBarrier
make_texture_barrier(RenderGraphTextureHandle handle, const RenderGraphTextureResource& resource,
                     const RenderGraphCompiledPass& source_pass,
                     const RenderGraphCompiledPass& destination_pass,
                     const LastTextureAccess& previous, RenderGraphTextureUsage next) {
    return RenderGraphTextureBarrier{
        .handle = handle,
        .source_pass_index = previous.pass_index,
        .source_usage = previous.usage,
        .destination_usage = next,
        .source_state = texture_usage_state(source_pass, resource, previous.usage),
        .destination_state = texture_usage_state(destination_pass, resource, next),
    };
}

[[nodiscard]] RenderGraphTextureBarrier make_texture_acquire_barrier(
    RenderGraphTextureHandle handle, const RenderGraphTextureState& source_state,
    const RenderGraphCompiledPass& destination_pass, const RenderGraphTextureResource& resource,
    RenderGraphTextureUsage next) {
    return RenderGraphTextureBarrier{
        .handle = handle,
        .source_pass_index = {},
        .source_usage = {},
        .destination_usage = next,
        .source_state = source_state,
        .destination_state = texture_usage_state(destination_pass, resource, next),
    };
}

[[nodiscard]] RenderGraphTextureBarrier make_texture_release_barrier(
    RenderGraphTextureHandle handle, const RenderGraphCompiledPass& source_pass,
    const RenderGraphTextureResource& resource, const LastTextureAccess& previous,
    const RenderGraphTextureState& destination_state) {
    return RenderGraphTextureBarrier{
        .handle = handle,
        .source_pass_index = previous.pass_index,
        .source_usage = previous.usage,
        .destination_usage = {},
        .source_state = texture_usage_state(source_pass, resource, previous.usage),
        .destination_state = destination_state,
    };
}

[[nodiscard]] RenderGraphBufferBarrier
make_buffer_barrier(RenderGraphBufferHandle handle, const RenderGraphCompiledPass& source_pass,
                    const RenderGraphCompiledPass& destination_pass,
                    const LastBufferAccess& previous, RenderGraphBufferUsage next) {
    return RenderGraphBufferBarrier{
        .handle = handle,
        .source_pass_index = previous.pass_index,
        .source_usage = previous.usage,
        .destination_usage = next,
        .source_state = buffer_usage_state(source_pass, previous.usage),
        .destination_state = buffer_usage_state(destination_pass, next),
    };
}

[[nodiscard]] RenderGraphBufferBarrier make_buffer_acquire_barrier(
    RenderGraphBufferHandle handle, const RenderGraphBufferState& source_state,
    const RenderGraphCompiledPass& destination_pass, RenderGraphBufferUsage next) {
    return RenderGraphBufferBarrier{
        .handle = handle,
        .source_pass_index = {},
        .source_usage = {},
        .destination_usage = next,
        .source_state = source_state,
        .destination_state = buffer_usage_state(destination_pass, next),
    };
}

[[nodiscard]] RenderGraphBufferBarrier make_buffer_release_barrier(
    RenderGraphBufferHandle handle, const RenderGraphCompiledPass& source_pass,
    const LastBufferAccess& previous, const RenderGraphBufferState& destination_state) {
    return RenderGraphBufferBarrier{
        .handle = handle,
        .source_pass_index = previous.pass_index,
        .source_usage = previous.usage,
        .destination_usage = {},
        .source_state = buffer_usage_state(source_pass, previous.usage),
        .destination_state = destination_state,
    };
}

template <typename AccessT, typename HandleT>
void validate_unique_accesses(const std::vector<AccessT>& accesses,
                              HandleT AccessT::* handle_member, const char* message) {
    for (auto current = accesses.begin(); current != accesses.end(); ++current) {
        const HandleT current_handle = (*current).*handle_member;
        const auto duplicate = std::find_if(
            accesses.begin(), current, [current_handle, handle_member](const AccessT& prior) {
                return (prior.*handle_member).index == current_handle.index;
            });
        if (duplicate != current) {
            throw std::runtime_error(message);
        }
    }
}

} // namespace

CompiledRenderGraph RenderGraphBuilder::compile() const {
    std::vector<RenderGraphCompiledPass> compiled_passes = passes_;
    std::vector<bool> texture_available(textures_.size(), false);
    std::vector<bool> buffer_available(buffers_.size(), false);
    std::vector<LastTextureAccess> last_texture_accesses(textures_.size());
    std::vector<LastBufferAccess> last_buffer_accesses(buffers_.size());

    for (std::size_t index = 0; index < textures_.size(); ++index) {
        texture_available[index] =
            textures_[index].lifetime == RenderGraphResourceLifetime::Imported;
    }
    for (std::size_t index = 0; index < buffers_.size(); ++index) {
        buffer_available[index] = buffers_[index].lifetime == RenderGraphResourceLifetime::Imported;
    }

    for (std::size_t pass_index = 0; pass_index < compiled_passes.size(); ++pass_index) {
        RenderGraphCompiledPass& pass = compiled_passes[pass_index];
        validate_unique_accesses(pass.texture_accesses, &RenderGraphTextureAccess::handle,
                                 "render graph pass cannot access a texture more than once");
        validate_unique_accesses(pass.buffer_accesses, &RenderGraphBufferAccess::handle,
                                 "render graph pass cannot access a buffer more than once");

        std::vector<std::size_t> texture_writes;
        std::vector<std::size_t> buffer_writes;

        for (const RenderGraphTextureAccess& access : pass.texture_accesses) {
            const RenderGraphTextureResource& resource = texture_resource(access.handle);
            const std::size_t index = static_cast<std::size_t>(access.handle.index - 1U);
            if (is_texture_read(access.usage) && !is_texture_write(access.usage) &&
                !texture_available[index]) {
                throw std::runtime_error("render graph transient texture is read before write");
            }
            const LastTextureAccess& previous = last_texture_accesses[index];
            if (previous.valid) {
                if (needs_texture_barrier(previous.usage, access.usage)) {
                    pass.before_texture_barriers.push_back(make_texture_barrier(
                        access.handle, resource, compiled_passes[previous.pass_index], pass,
                        previous, access.usage));
                }
            } else if (resource.initial_state.has_value()) {
                pass.before_texture_barriers.push_back(make_texture_acquire_barrier(
                    access.handle, resource.initial_state.value(), pass, resource, access.usage));
            } else if (resource.lifetime == RenderGraphResourceLifetime::Transient) {
                pass.before_texture_barriers.push_back(
                    make_texture_acquire_barrier(access.handle, transient_initial_texture_state(),
                                                 pass, resource, access.usage));
            }
            if (is_texture_write(access.usage)) {
                texture_writes.push_back(index);
            }
            last_texture_accesses[index] = LastTextureAccess{
                .valid = true,
                .pass_index = pass_index,
                .usage = access.usage,
            };
        }

        for (const RenderGraphBufferAccess& access : pass.buffer_accesses) {
            const RenderGraphBufferResource& resource = buffer_resource(access.handle);
            const std::size_t index = static_cast<std::size_t>(access.handle.index - 1U);
            if (is_buffer_read(access.usage) && !is_buffer_write(access.usage) &&
                !buffer_available[index]) {
                throw std::runtime_error("render graph transient buffer is read before write");
            }
            const LastBufferAccess& previous = last_buffer_accesses[index];
            if (previous.valid) {
                if (needs_buffer_barrier(previous.usage, access.usage)) {
                    pass.before_buffer_barriers.push_back(
                        make_buffer_barrier(access.handle, compiled_passes[previous.pass_index],
                                            pass, previous, access.usage));
                }
            } else if (resource.initial_state.has_value()) {
                pass.before_buffer_barriers.push_back(make_buffer_acquire_barrier(
                    access.handle, resource.initial_state.value(), pass, access.usage));
            }
            if (is_buffer_write(access.usage)) {
                buffer_writes.push_back(index);
            }
            last_buffer_accesses[index] = LastBufferAccess{
                .valid = true,
                .pass_index = pass_index,
                .usage = access.usage,
            };
            static_cast<void>(resource);
        }

        for (std::size_t index : texture_writes) {
            texture_available[index] = true;
        }
        for (std::size_t index : buffer_writes) {
            buffer_available[index] = true;
        }
    }

    for (std::size_t index = 0; index < textures_.size(); ++index) {
        const RenderGraphTextureResource& resource = textures_[index];
        const LastTextureAccess& previous = last_texture_accesses[index];
        if (!previous.valid || !resource.final_state.has_value()) {
            continue;
        }
        compiled_passes[previous.pass_index].after_texture_barriers.push_back(
            make_texture_release_barrier(resource.handle, compiled_passes[previous.pass_index],
                                         resource, previous, resource.final_state.value()));
    }

    for (std::size_t index = 0; index < buffers_.size(); ++index) {
        const RenderGraphBufferResource& resource = buffers_[index];
        const LastBufferAccess& previous = last_buffer_accesses[index];
        if (!previous.valid || !resource.final_state.has_value()) {
            continue;
        }
        compiled_passes[previous.pass_index].after_buffer_barriers.push_back(
            make_buffer_release_barrier(resource.handle, compiled_passes[previous.pass_index],
                                        previous, resource.final_state.value()));
    }

    return CompiledRenderGraph(textures_, buffers_, compiled_passes);
}

} // namespace cubey::render
