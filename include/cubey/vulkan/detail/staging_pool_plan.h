#pragma once

#include <cubey/vulkan/submission_tickets.h>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cubey::vulkan::detail {

struct StagingPoolPlanConfig {
    VkDeviceSize initial_block_byte_size = 0;
    VkDeviceSize growth_block_byte_size = 0;
    VkDeviceSize max_total_byte_size = 0;
};

struct StagingPoolReservationPlan {
    std::uint64_t id = 0;
    std::size_t block_index = 0;
    VkDeviceSize offset = 0;
    VkDeviceSize byte_size = 0;
};

// Pure range planner used by the runtime-owned staging pool. It reserves
// aligned spans immediately, then makes them reusable only after the submitted
// graphics ticket is known complete.
class StagingPoolPlanner {
  public:
    explicit StagingPoolPlanner(StagingPoolPlanConfig config);

    [[nodiscard]] std::optional<StagingPoolReservationPlan> try_reserve(VkDeviceSize byte_size,
                                                                        VkDeviceSize alignment);
    void commit(std::uint64_t reservation_id, GpuSubmissionTicket retirement_ticket);
    void commit_many(std::span<const std::uint64_t> reservation_ids,
                     GpuSubmissionTicket retirement_ticket);
    void cancel(std::uint64_t reservation_id);
    [[nodiscard]] std::size_t reclaim(GpuSubmissionTicket completed_ticket);

    [[nodiscard]] std::size_t block_count() const noexcept;
    [[nodiscard]] VkDeviceSize block_byte_size(std::size_t block_index) const;
    [[nodiscard]] VkDeviceSize total_capacity_byte_size() const noexcept;
    [[nodiscard]] VkDeviceSize reserved_byte_size() const noexcept;

  private:
    struct FreeRange {
        VkDeviceSize offset = 0;
        VkDeviceSize byte_size = 0;
    };
    struct Allocation {
        StagingPoolReservationPlan reservation{};
        std::optional<GpuSubmissionTicket> retirement_ticket{};
    };
    struct Block {
        VkDeviceSize byte_size = 0;
        std::vector<FreeRange> free_ranges{};
        std::vector<Allocation> allocations{};
    };

    [[nodiscard]] std::optional<StagingPoolReservationPlan>
    try_reserve_from_block(std::size_t block_index, VkDeviceSize byte_size, VkDeviceSize alignment);
    [[nodiscard]] Allocation* find_allocation(std::uint64_t reservation_id);
    void release(Allocation allocation);
    void add_block(VkDeviceSize byte_size);

    StagingPoolPlanConfig config_{};
    std::vector<Block> blocks_{};
    std::uint64_t next_reservation_id_ = 1;
    VkDeviceSize total_capacity_byte_size_ = 0;
    VkDeviceSize reserved_byte_size_ = 0;
};

} // namespace cubey::vulkan::detail
