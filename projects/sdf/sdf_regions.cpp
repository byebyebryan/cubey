#include "sdf_regions.h"
#include <cubey/engine/project_gpu_services.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
constexpr std::uint32_t neg = 0x80000000U;
float combine(float a, float b, const SceneInstruction& n) {
    const auto code = static_cast<unsigned>(n.center_code.w);
    if (code == 10)
        return std::min(a, b);
    if (code == 11)
        return std::max(a, -b);
    const float k = n.settings.y;
    if (code == 13) {
        const float h = std::clamp(.5F + .5F * (a + b) / k, 0.F, 1.F);
        return std::lerp(-b, a, h) + k * h * (1 - h);
    }
    const float h = std::clamp(.5F + .5F * (b - a) / k, 0.F, 1.F);
    return std::lerp(b, a, h) - k * h * (1 - h);
}
float eval(math::Vec3 p, const AuthoredScene& scene, std::span<const std::uint32_t> refs) {
    std::array<float, 32> stack{};
    unsigned size = 0;
    for (auto ref : refs) {
        const auto& n = scene.program.at(ref & ~neg);
        float d;
        if (n.center_code.w < 10)
            d = authored_primitive_distance(p, n);
        else {
            const auto b = stack.at(--size);
            const auto a = stack.at(--size);
            d = combine(a, b, n);
        }
        stack.at(size++) = (ref & neg) ? -d : d;
    }
    if (size != 1)
        throw std::runtime_error("invalid region program");
    const auto q = glm::abs(p) - math::Vec3{.499F};
    const float clip =
        glm::length(glm::max(q, math::Vec3{0})) + std::min(std::max({q.x, q.y, q.z}), 0.F);
    return std::max(stack[0], clip);
}
} // namespace
float authored_evaluation_guard(const AuthoredScene& scene) {
    float magnitude = 1;
    float blend_offset = 0;
    for (const auto& n : scene.program) {
        if (n.center_code.w < 10)
            magnitude = std::max(magnitude, 1 + glm::length(math::Vec3(n.center_code)) +
                                                glm::length(n.parameters) * n.settings.x);
        else if (n.center_code.w >= 12)
            blend_offset += n.settings.y * .25F;
    }
    magnitude += blend_offset;
    constexpr float u = std::numeric_limits<float>::epsilon();
    std::array<float, 32> stack{};
    unsigned size = 0;
    for (const auto& n : scene.program) {
        if (n.center_code.w < 10)
            stack.at(size++) = 64 * u * magnitude;
        else {
            const float b = stack.at(--size), a = stack.at(--size);
            // min/max and sign changes introduce no arithmetic rounding. The
            // smooth operator is non-expansive in its two child field values;
            // budget its own arithmetic along the dependency path.
            stack.at(size++) = std::max(a, b) + (n.center_code.w >= 12 ? 16 * u * magnitude : 0.F);
        }
    }
    return size ? stack[0] + 8 * u * magnitude : 0;
}
RegionPrograms build_region_programs(const AuthoredScene& scene, std::uint32_t axis,
                                     std::size_t limit) {
    if (axis != 4 && axis != 16 && axis != 64)
        throw std::invalid_argument("region axis must be 4, 16 or 64");
    const auto start = std::chrono::steady_clock::now();
    RegionPrograms parent;
    parent.headers.push_back({0, static_cast<std::uint32_t>(scene.program.size())});
    parent.references.resize(scene.program.size());
    std::iota(parent.references.begin(), parent.references.end(), 0U);
    if (scene.program.empty()) {
        parent.axis = 0;
        return parent;
    }
    const float guard = 2 * authored_evaluation_guard(scene);
    struct Entry {
        float value;
        int a = -1, b = -1, choice = -1;
        std::uint32_t ref;
    };
    std::vector<Entry> entries;
    entries.reserve(scene.program.size());
    std::array<int, 32> stack{};
    std::vector<std::uint32_t> compact;
    compact.reserve(scene.program.size());
    for (std::uint32_t dim = 4; dim <= axis; dim *= 4) {
        RegionPrograms next;
        next.axis = dim;
        next.arithmetic_guard = guard;
        next.headers.resize(static_cast<std::size_t>(dim) * dim * dim);
        // Header count==0 is an explicit whole-source fallback, never an empty solid.
        const float radius = std::sqrt(3.F) * .5F / static_cast<float>(dim);
        const auto pdim = dim / 4;
        for (std::uint32_t z = 0; z < dim; ++z)
            for (std::uint32_t y = 0; y < dim; ++y)
                for (std::uint32_t x = 0; x < dim; ++x) {
                    const auto index = (z * dim + y) * dim + x;
                    const auto parent_index =
                        dim == 4 ? 0 : ((z / 4 * pdim + y / 4) * pdim + x / 4);
                    const auto h = parent.headers[parent_index];
                    const auto refs = h.count ? std::span<const std::uint32_t>(parent.references)
                                                    .subspan(h.offset, h.count)
                                              : std::span<const std::uint32_t>{};
                    if (refs.empty()) {
                        next.headers[index] = {0, 0};
                        ++next.fallback_cells;
                        continue;
                    }
                    const math::Vec3 p =
                        (math::Vec3{x, y, z} + .5F) / static_cast<float>(dim) - .5F;
                    entries.clear();
                    unsigned size = 0;
                    for (auto ref : refs) {
                        const auto& n = scene.program[ref & ~neg];
                        Entry e{};
                        e.ref = ref;
                        if (n.center_code.w < 10)
                            e.value = authored_primitive_distance(p, n);
                        else {
                            e.b = stack.at(--size);
                            e.a = stack.at(--size);
                            const float a = entries[static_cast<std::size_t>(e.a)].value,
                                        b = entries[static_cast<std::size_t>(e.b)].value;
                            const bool sub = n.center_code.w == 11 || n.center_code.w == 13;
                            const float cb = sub ? -b : b;
                            const float k = n.center_code.w >= 12 ? n.settings.y : 0.F;
                            if (std::abs(a - cb) > 2 * radius + k + guard)
                                e.choice = (sub ? a > cb : a < cb) ? 0 : 1;
                            e.value = combine(a, b, n);
                        }
                        if (ref & neg)
                            e.value = -e.value;
                        stack.at(size++) = static_cast<int>(entries.size());
                        entries.push_back(e);
                    }
                    compact.clear();
                    const auto emit = [&](auto&& self, int id, bool flip) -> void {
                        const auto& e = entries[static_cast<std::size_t>(id)];
                        const auto code = scene.program[e.ref & ~neg].center_code.w;
                        if (e.choice >= 0) {
                            const bool sub = code == 11 || code == 13;
                            self(self, e.choice == 0 ? e.a : e.b,
                                 flip ^ bool(e.ref & neg) ^ (e.choice == 1 && sub));
                        } else {
                            if (e.a >= 0) {
                                self(self, e.a, false);
                                self(self, e.b, false);
                            }
                            compact.push_back(e.ref ^ (flip ? neg : 0U));
                        }
                    };
                    emit(emit, stack[0], false);
                    if (next.references.size() + compact.size() > limit) {
                        next.headers[index] = {0, 0};
                        ++next.fallback_cells;
                    } else {
                        next.headers[index] = {static_cast<std::uint32_t>(next.references.size()),
                                               static_cast<std::uint32_t>(compact.size())};
                        next.references.insert(next.references.end(), compact.begin(),
                                               compact.end());
                    }
                }
        next.scratch_peak_bytes = std::max(
            parent.scratch_peak_bytes,
            static_cast<std::uint64_t>(
                (parent.headers.capacity() + next.headers.capacity()) *
                    sizeof(RegionPrograms::Header) +
                (parent.references.capacity() + next.references.capacity() + compact.capacity()) *
                    4 +
                entries.capacity() * sizeof(Entry)));
        parent = std::move(next);
    }
    parent.build_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return parent;
}
float region_distance(math::Vec3 p, const AuthoredScene& scene, const RegionPrograms& regions) {
    if (!regions.axis)
        return authored_distance(p, scene);
    const auto cell =
        glm::clamp(glm::ivec3(glm::floor((p + .5F) * static_cast<float>(regions.axis))),
                   glm::ivec3{0}, glm::ivec3{static_cast<int>(regions.axis) - 1});
    const auto i = static_cast<std::size_t>((cell.z * static_cast<int>(regions.axis) + cell.y) *
                                                static_cast<int>(regions.axis) +
                                            cell.x);
    const auto h = regions.headers.at(i);
    return h.count
               ? eval(p, scene,
                      std::span<const std::uint32_t>(regions.references).subspan(h.offset, h.count))
               : authored_distance(p, scene);
}
GpuRegionPrograms::GpuRegionPrograms(vulkan::Device& device, const AuthoredScene& scene,
                                     std::uint32_t dimension, std::size_t limit,
                                     bool device_local, ProjectGpuServices* uploads) {
    const auto start = std::chrono::steady_clock::now();
    const auto cpu = build_region_programs(scene, dimension, limit);
    axis = cpu.axis;
    fallback_cells = cpu.fallback_cells;
    reference_count = cpu.references.size();
    scratch_peak_bytes = cpu.scratch_peak_bytes;
    geometry_hash = authored_geometry_hash(scene);
    const auto allocate = [&](std::size_t size) {
        return std::make_unique<vulkan::Buffer>(
            device,
            vulkan::BufferConfig{.size = size,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 .memory_properties =
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                     (device_local ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0U)});
    };
    const std::array<std::uint32_t, 4> header{axis, 0, 0, 0};
    if (uploads) {
        std::vector<std::uint32_t> data(header.begin(), header.end());
        for (const auto h : cpu.headers) {
            data.push_back(h.offset);
            data.push_back(h.count);
        }
        auto refs = cpu.references;
        if (refs.empty()) refs.push_back(0);
        const std::array<vulkan::DeviceBufferUpload, 2> requests{{
            {data.data(), data.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT},
            {refs.data(), refs.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT}}};
        auto batch = uploads->upload_device_buffers(requests, "sdf acceleration regions");
        cells = std::make_unique<vulkan::Buffer>(std::move(batch.buffers[0]));
        references = std::make_unique<vulkan::Buffer>(std::move(batch.buffers[1]));
    } else {
        cells = allocate(16 + cpu.headers.size() * sizeof(RegionPrograms::Header));
        references = allocate(std::max<std::size_t>(4, cpu.references.size() * 4));
        cells->upload(header.data(), 16);
        if (!cpu.headers.empty())
            cells->upload(cpu.headers.data(), cpu.headers.size() * sizeof(RegionPrograms::Header), 16);
        if (!cpu.references.empty())
            references->upload(cpu.references.data(), cpu.references.size() * 4);
    }
    allocation_bytes = cells->size() + references->size();
    build_upload_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
} // namespace cubey::projects::sdf
