#include <cubey/render/atmosphere_reflection_probe.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/instance.h>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace cubey::vulkan;
    try {
        InstanceConfig instance_config;
        instance_config.application_name = "raytracing-device-contract";
        instance_config.require_validation = true;
        Instance instance(instance_config);
        DeviceConfig config;
        config.require_present = false;
        config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        Device ordinary(instance, config);
        if (ordinary.ray_tracing_enabled())
            throw std::runtime_error("ordinary device enabled RT");
        bool rejected = false;
        try {
            Buffer forbidden(ordinary, {64, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("address allocation did not reject disabled capability");
        for (auto mask : {VkPipelineStageFlags(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR),
                          VkPipelineStageFlags(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)}) {
            bool stage_rejected = false;
            try {
                cubey::render::AtmosphereReflectionProbe probe;
                cubey::render::AtmosphereReflectionProbeConfig request;
                request.consumer_stage_mask = mask;
                probe.create_resources(ordinary, request);
            } catch (const std::invalid_argument& e) {
                stage_rejected =
                    std::string_view(e.what()).find("consumer stages") != std::string_view::npos;
            }
            if (!stage_rejected)
                throw std::runtime_error("probe accepted unsupported consumer stages");
        }
        config.require_ray_tracing = true;
        Device tracing(instance, config);
        if (!tracing.ray_tracing_enabled())
            throw std::runtime_error("RT device flag missing");
        Buffer buffer(
            tracing,
            {256, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});
        if (!buffer.device_address())
            throw std::runtime_error("zero buffer device address");
        Buffer moved(std::move(buffer));
        if (!moved.device_address())
            throw std::runtime_error("address lost by buffer move");
        rejected = false;
        try {
            (void)buffer.device_address();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("moved-from buffer retained an address");
        std::cout << "raytracing: opt-in device and address lifetime pass\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
