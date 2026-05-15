#include "source_file_test_helpers.h"

#include <cubey/render/deformation.h>
#include <cubey/render/pipeline_resource.h>

#include <vulkan/vulkan.h>

#include <array>
#include <filesystem>
#include <stdexcept>

namespace {

using cubey::tests::read_source_file;
using cubey::tests::require_contains;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_gpu_deformation_descriptor_set_declares_storage_buffers() {
    const cubey::vulkan::DescriptorSetInfo info =
        cubey::render::gpu_deformation_descriptor_set_info();

    require(info.bindings().size() == 6,
            "deformation descriptor set should bind all deformation buffers");
    for (const VkDescriptorSetLayoutBinding& binding : info.bindings()) {
        require(binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                "deformation descriptor bindings should be storage buffers");
        require((binding.stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) != 0,
                "deformation descriptor bindings should be visible to compute");
    }
}

void test_gpu_deformation_descriptor_set_scales_for_frame_slots() {
    const cubey::vulkan::DescriptorSetInfo info =
        cubey::render::gpu_deformation_descriptor_set_info(3);

    require(info.max_sets() == 3, "deformation descriptor set should support frame slots");
    require(info.pool_info().maxSets == 3,
            "deformation descriptor pool should allocate one set per frame slot");
}

void test_gpu_deformation_dispatch_groups_round_up_vertices() {
    const cubey::render::GpuDeformationDispatch dispatch =
        cubey::render::gpu_deformation_dispatch_groups(130, 64);

    require(dispatch.x == 3, "deformation dispatch should round up vertex groups");
    require(dispatch.y == 1 && dispatch.z == 1, "deformation dispatch should be one dimensional");
}

void test_gpu_deformation_pipeline_config_uses_compute_stage_and_push_constants() {
    const cubey::render::ComputePipelineResourceConfig config =
        cubey::render::gpu_deformation_pipeline_config("gltf_deform.comp.spv");

    require(config.shader_stage.stage == VK_SHADER_STAGE_COMPUTE_BIT,
            "deformation pipeline should use a compute shader");
    require(config.push_constants.size() == 1,
            "deformation pipeline should declare one push constant range");
    require(config.push_constants[0].size == sizeof(cubey::render::GpuDeformationPushConstants),
            "deformation push constant range should match CPU struct");
}

void test_gpu_deformation_pipeline_config_accepts_descriptor_layouts() {
    const VkDescriptorSetLayout layout = reinterpret_cast<VkDescriptorSetLayout>(0x1234);
    const std::array<VkDescriptorSetLayout, 1> layouts{layout};
    const cubey::render::ComputePipelineResourceConfig config =
        cubey::render::gpu_deformation_pipeline_config("gltf_deform.comp.spv", layouts);

    require(config.descriptor_set_layouts.size() == 1,
            "deformation pipeline should accept descriptor set layouts");
    require(config.descriptor_set_layouts[0] == layout,
            "deformation pipeline should preserve descriptor set layout handle");
}

void test_gpu_deformation_shader_morphs_before_skinning() {
    const std::filesystem::path root = CUBEY_SOURCE_DIR;
    const std::string shader =
        read_source_file(root / "projects/gltf_viewer/shaders/gltf_deform.comp");

    require_contains(shader, "applyMorphTargets", "deformation shader should morph vertices");
    require_contains(shader, "applySkinning", "deformation shader should skin vertices");
    require(shader.find("applyMorphTargets") < shader.find("applySkinning"),
            "deformation shader should morph before skinning");
    require_contains(shader, "const uint kPbrVertexFloatCount = 18u",
                     "deformation shader should match the expanded PBR vertex layout");
    require_contains(shader, "vertex.uv1",
                     "deformation shader should preserve the second UV set");
    require_contains(shader, "vertex.color0",
                     "deformation shader should preserve vertex colors");
}
