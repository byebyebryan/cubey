#include <array>
#include <cmath>
#include <cubey/render/pipeline_resource.h>
#include <cubey/terrain/terrain_surface_context.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>
#include <iostream>
int main(int argc, char** argv) {
    try {
        using namespace cubey;
        const auto source_path = argc == 3 ? std::filesystem::path(argv[2])
                                           : std::filesystem::path(CUBEY_TEST_ASSET_DIR) /
                                                 "terrain/backdrop-smoke/heightfield.json";
        asset::TerrainRasterHeightSource source(source_path);
        asset::TerrainWaterCapture c;
        if (argc == 3)
            c = asset::load_terrain_water_capture(argv[1]);
        else {
            c.grid = {7U, 5U, 128.0F, {4096.0F, 1024.0F}};
            c.source_elevation_sha256 = source.provenance().elevation_sha256;
            c.capture_time_s = 100;
            c.rain_mm_per_hour = 512;
            c.recording_sha256 = std::string(64, 'a');
            c.frame_sha256 = std::string(64, 'b');
            for (unsigned z = 0; z < 5; ++z)
                for (unsigned x = 0; x < 7; ++x) {
                    c.bed_m.push_back(
                        source.sample_height({.world_xz = c.grid.sample_origin_xz_m +
                                                          math::Vec2{x, z} * c.grid.cell_size_m}));
                    c.depth_m.push_back(1);
                    c.velocity_xz_m_per_s.push_back({});
                }
        }
        const auto product = terrain::make_terrain_surface_context(
            c, source, terrain::make_terrain_surface_field(source, source.bounds(), nullptr, 64));
        const auto count = product.inner_perimeter_vertices;
        vulkan::Instance instance({.application_name = "terrain context GPU edge controls",
                                   .application_version = 0U,
                                   .required_extensions = {},
                                   .validation = true,
                                   .require_validation = false});
        vulkan::Device device(instance, {.required_queue_flags = VK_QUEUE_COMPUTE_BIT,
                                         .require_present = false,
                                         .require_dynamic_rendering = false});
        vulkan::SubmissionCoordinator submission(device);
        const auto host = [&](const void* data, VkDeviceSize bytes) {
            vulkan::Buffer b(device, {.size = bytes,
                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                      .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
            b.upload(data, bytes);
            return b;
        };
        auto bed = host(c.bed_m.data(), c.bed_m.size() * sizeof(float));
        auto depth = host(c.depth_m.data(), c.depth_m.size() * sizeof(float));
        auto edge =
            host(product.vertices.data(), count * sizeof(terrain::TerrainSurfaceContextVertex));
        const auto bytes = count * sizeof(math::Vec4);
        vulkan::Buffer output(device, vulkan::device_local_buffer_config(
                                          bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        vulkan::Buffer readback(device, vulkan::readback_buffer_config(bytes));
        std::array<vulkan::DescriptorSetBindingConfig, 4> bindings;
        for (unsigned i = 0; i < 4; ++i)
            bindings[i] = {.binding = i,
                           .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT};
        vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
        vulkan::DescriptorWriteBatch writes;
        const std::array buffers{&bed, &depth, &edge, &output};
        for (unsigned i = 0; i < 4; ++i)
            writes.storage_buffer(descriptors.set(), i, buffers[i]->handle(), buffers[i]->size());
        writes.update(device);
        const std::array layouts{descriptors.layout()};
        const std::array ranges{VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16}};
        render::ComputePipelineResource pipeline(
            device, {.shader_stage = render::compute_shader_file(
                         std::filesystem::path(CUBEY_TERRAIN_PATCH_TEST_SHADER_DIR) /
                         "terrain_surface_context_edge.comp.spv"),
                     .descriptor_set_layouts = layouts,
                     .push_constants = ranges});
        vulkan::ImmediateCommands commands(device, submission);
        vulkan::CommandRecorder r(commands.command_buffer());
        r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
        r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                              descriptors.set());
        r.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         math::Vec4{float(c.grid.width), float(c.grid.height), c.grid.cell_size_m,
                                    float(count)});
        r.dispatch((count + 63) / 64, 1, 1);
        const VkBufferMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                            .pNext = nullptr,
                                            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                            .buffer = output.handle(),
                                            .offset = 0,
                                            .size = bytes};
        r.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                           {}, {&barrier, 1}, {});
        const VkBufferCopy copy{0, 0, bytes};
        vkCmdCopyBuffer(r.handle(), output.handle(), readback.handle(), 1, &copy);
        commands.submit_and_wait();
        std::vector<math::Vec4> values(count);
        readback.download(values.data(), bytes);
        float worst = 0, normal = 0;
        for (const auto v : values) {
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || v.x > .001F || v.y > .0001F)
                throw std::runtime_error("CPU/GPU context perimeter mismatch");
            worst = std::max(worst, v.x);
            normal = std::max(normal, v.y);
        }
        std::cout << "terrain_surface_context_edge: PASS samples=" << count
                  << " max_height_error_m=" << worst << " max_normal_error=" << normal << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return std::string_view(e.what()).find("no Vulkan") != std::string_view::npos ? 77 : 1;
    }
}
