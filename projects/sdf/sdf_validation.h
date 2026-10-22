#pragma once
#include "sdf_field.h"
namespace cubey::projects::sdf {
void validate_field(vulkan::Device& device, ProjectGpuServices& gpu, const Field& field,
                    const Recipe& recipe, const AuthoredScene* authored = nullptr);
void validate_authored(vulkan::Device& device, ProjectGpuServices& gpu, const AuthoredScene& scene);
void validate_batches(vulkan::Device& device, ProjectGpuServices& gpu, Field& field);
void validate_subtractions(vulkan::Device& device, ProjectGpuServices& gpu, Field& field);
} // namespace cubey::projects::sdf
