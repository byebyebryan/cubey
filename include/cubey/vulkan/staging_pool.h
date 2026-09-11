#pragma once

#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/detail/staging_pool_plan.h>
#include <cubey/vulkan/staging_pool_config.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cubey::vulkan {

class GpuOwnerContext;
class GpuRuntime;

struct GpuStagingPoolStats {
    std::size_t block_count = 0;
    VkDeviceSize capacity_byte_size = 0;
    VkDeviceSize reserved_byte_size = 0;
};

struct GpuStagingReservation {
    GpuStagingReservation() = default;
    ~GpuStagingReservation() = default;

    GpuStagingReservation(const GpuStagingReservation&) = delete;
    GpuStagingReservation& operator=(const GpuStagingReservation&) = delete;
    GpuStagingReservation(GpuStagingReservation&& other) noexcept
        : buffer(std::exchange(other.buffer, VK_NULL_HANDLE)),
          mapped(std::exchange(other.mapped, nullptr)), offset(std::exchange(other.offset, 0)),
          byte_size(std::exchange(other.byte_size, 0)), plan(std::exchange(other.plan, {})) {}
    GpuStagingReservation& operator=(GpuStagingReservation&&) = delete;

    VkBuffer buffer = VK_NULL_HANDLE;
    std::byte* mapped = nullptr;
    VkDeviceSize offset = 0;
    VkDeviceSize byte_size = 0;

  private:
    friend class GpuStagingPool;
    detail::StagingPoolReservationPlan plan{};
};

// Runtime-owned host-visible transfer-source blocks. All mutation and Vulkan
// lifetime work is confined to the GPU owner. A nullopt reservation is normal
// backpressure: in-flight ranges consume the bounded pool and a later upload
// step must retry after graphics completion advances.
class GpuStagingPool {
  public:
    ~GpuStagingPool();

    GpuStagingPool(const GpuStagingPool&) = delete;
    GpuStagingPool& operator=(const GpuStagingPool&) = delete;
    GpuStagingPool(GpuStagingPool&&) = delete;
    GpuStagingPool& operator=(GpuStagingPool&&) = delete;

    void initialize(GpuOwnerContext& context);
    [[nodiscard]] std::optional<GpuStagingReservation>
    try_reserve(GpuOwnerContext& context, VkDeviceSize byte_size, VkDeviceSize alignment);
    void commit(GpuOwnerContext& context, GpuStagingReservation& reservation,
                GpuSubmissionTicket retirement_ticket);
    void commit_many(GpuOwnerContext& context, std::span<GpuStagingReservation> reservations,
                     GpuSubmissionTicket retirement_ticket);
    void cancel(GpuOwnerContext& context, GpuStagingReservation& reservation);
    [[nodiscard]] std::size_t reclaim(GpuOwnerContext& context);
    void shutdown_on_owner_thread(GpuOwnerContext& context);

    [[nodiscard]] GpuStagingPoolStats stats() const noexcept;

  private:
    friend class GpuRuntime;

    explicit GpuStagingPool(GpuStagingPoolConfig config);

    struct Block {
        std::optional<Buffer> buffer{};
        std::byte* mapped = nullptr;
    };

    void ensure_physical_block(GpuOwnerContext& context, std::size_t block_index);
    [[nodiscard]] static detail::StagingPoolPlanConfig
    planner_config(GpuStagingPoolConfig config) noexcept;

    detail::StagingPoolPlanner planner_;
    std::vector<Block> blocks_{};
    bool initialized_ = false;
};

} // namespace cubey::vulkan
