#include <cubey/vulkan/detail/staging_pool_plan.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cubey::vulkan::detail {
namespace {

[[nodiscard]] VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment == 0) {
        throw std::runtime_error("staging pool alignment must be positive");
    }
    const VkDeviceSize remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    const VkDeviceSize padding = alignment - remainder;
    if (value > std::numeric_limits<VkDeviceSize>::max() - padding) {
        throw std::runtime_error("staging pool alignment overflows");
    }
    return value + padding;
}

} // namespace

StagingPoolPlanner::StagingPoolPlanner(StagingPoolPlanConfig config) : config_(config) {
    if (config_.initial_block_byte_size == 0 || config_.growth_block_byte_size == 0 ||
        config_.max_total_byte_size < config_.initial_block_byte_size) {
        throw std::runtime_error("staging pool capacity configuration is invalid");
    }
    add_block(config_.initial_block_byte_size);
}

std::optional<StagingPoolReservationPlan> StagingPoolPlanner::try_reserve(VkDeviceSize byte_size,
                                                                          VkDeviceSize alignment) {
    if (byte_size == 0) {
        throw std::runtime_error("staging pool reservation size must be positive");
    }
    for (std::size_t index = 0; index < blocks_.size(); ++index) {
        if (auto reservation = try_reserve_from_block(index, byte_size, alignment);
            reservation.has_value()) {
            return reservation;
        }
    }

    const VkDeviceSize required_block_byte_size = align_up(byte_size, alignment);
    const VkDeviceSize new_block_byte_size =
        std::max(config_.growth_block_byte_size, required_block_byte_size);
    if (new_block_byte_size > config_.max_total_byte_size - total_capacity_byte_size_) {
        return std::nullopt;
    }
    add_block(new_block_byte_size);
    return try_reserve_from_block(blocks_.size() - 1U, byte_size, alignment);
}

void StagingPoolPlanner::commit(std::uint64_t reservation_id,
                                GpuSubmissionTicket retirement_ticket) {
    commit_many({&reservation_id, 1U}, retirement_ticket);
}

void StagingPoolPlanner::commit_many(std::span<const std::uint64_t> reservation_ids,
                                     GpuSubmissionTicket retirement_ticket) {
    if (retirement_ticket.value == 0) {
        throw std::runtime_error("staging pool retirement requires a submission ticket");
    }
    if (reservation_ids.empty()) {
        throw std::runtime_error("staging pool commit requires at least one reservation");
    }

    // Validate the complete set before mutating any allocation. A real queue
    // submission has already happened at this point, so partial retirement
    // state would make staging lifetime impossible to recover safely.
    std::vector<Allocation*> allocations;
    allocations.reserve(reservation_ids.size());
    for (std::size_t index = 0; index < reservation_ids.size(); ++index) {
        const std::uint64_t reservation_id = reservation_ids[index];
        if (reservation_id == 0) {
            throw std::runtime_error("staging pool reservation is not active");
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (reservation_ids[earlier] == reservation_id) {
                throw std::runtime_error("staging pool commit has duplicate reservations");
            }
        }
        Allocation* allocation = find_allocation(reservation_id);
        if (allocation == nullptr) {
            throw std::runtime_error("staging pool reservation is unknown");
        }
        if (allocation->retirement_ticket.has_value()) {
            throw std::runtime_error("staging pool reservation was already committed");
        }
        allocations.push_back(allocation);
    }

    for (Allocation* allocation : allocations) {
        allocation->retirement_ticket = retirement_ticket;
    }
}

void StagingPoolPlanner::cancel(std::uint64_t reservation_id) {
    for (Block& block : blocks_) {
        for (auto iterator = block.allocations.begin(); iterator != block.allocations.end();
             ++iterator) {
            if (iterator->reservation.id == reservation_id) {
                if (iterator->retirement_ticket.has_value()) {
                    throw std::runtime_error("cannot cancel a submitted staging pool reservation");
                }
                Allocation allocation = std::move(*iterator);
                block.allocations.erase(iterator);
                release(std::move(allocation));
                return;
            }
        }
    }
    throw std::runtime_error("staging pool reservation is unknown");
}

std::size_t StagingPoolPlanner::reclaim(GpuSubmissionTicket completed_ticket) {
    std::size_t reclaimed = 0;
    for (Block& block : blocks_) {
        std::vector<Allocation> remaining;
        remaining.reserve(block.allocations.size());
        for (Allocation& allocation : block.allocations) {
            if (allocation.retirement_ticket.has_value() &&
                allocation.retirement_ticket.value() <= completed_ticket) {
                release(std::move(allocation));
                ++reclaimed;
            } else {
                remaining.push_back(std::move(allocation));
            }
        }
        block.allocations = std::move(remaining);
    }
    return reclaimed;
}

std::size_t StagingPoolPlanner::block_count() const noexcept {
    return blocks_.size();
}

VkDeviceSize StagingPoolPlanner::block_byte_size(std::size_t block_index) const {
    if (block_index >= blocks_.size()) {
        throw std::runtime_error("staging pool block index is outside the pool");
    }
    return blocks_[block_index].byte_size;
}

VkDeviceSize StagingPoolPlanner::total_capacity_byte_size() const noexcept {
    return total_capacity_byte_size_;
}

VkDeviceSize StagingPoolPlanner::reserved_byte_size() const noexcept {
    return reserved_byte_size_;
}

std::optional<StagingPoolReservationPlan>
StagingPoolPlanner::try_reserve_from_block(std::size_t block_index, VkDeviceSize byte_size,
                                           VkDeviceSize alignment) {
    Block& block = blocks_[block_index];
    for (std::size_t range_index = 0; range_index < block.free_ranges.size(); ++range_index) {
        FreeRange range = block.free_ranges[range_index];
        const VkDeviceSize offset = align_up(range.offset, alignment);
        if (offset < range.offset || offset - range.offset > range.byte_size) {
            continue;
        }
        const VkDeviceSize prefix_byte_size = offset - range.offset;
        const VkDeviceSize available_byte_size = range.byte_size - prefix_byte_size;
        if (byte_size > available_byte_size) {
            continue;
        }
        const StagingPoolReservationPlan reservation{
            .id = next_reservation_id_++,
            .block_index = block_index,
            .offset = offset,
            .byte_size = byte_size,
        };
        std::vector<FreeRange> replacement;
        if (prefix_byte_size != 0) {
            replacement.push_back({.offset = range.offset, .byte_size = prefix_byte_size});
        }
        const VkDeviceSize suffix_offset = offset + byte_size;
        const VkDeviceSize suffix_byte_size = available_byte_size - byte_size;
        if (suffix_byte_size != 0) {
            replacement.push_back({.offset = suffix_offset, .byte_size = suffix_byte_size});
        }
        block.free_ranges.erase(block.free_ranges.begin() +
                                static_cast<std::ptrdiff_t>(range_index));
        block.free_ranges.insert(block.free_ranges.begin() +
                                     static_cast<std::ptrdiff_t>(range_index),
                                 replacement.begin(), replacement.end());
        block.allocations.push_back(
            {.reservation = reservation, .retirement_ticket = std::nullopt});
        reserved_byte_size_ += byte_size;
        return reservation;
    }
    return std::nullopt;
}

StagingPoolPlanner::Allocation* StagingPoolPlanner::find_allocation(std::uint64_t reservation_id) {
    for (Block& block : blocks_) {
        for (Allocation& allocation : block.allocations) {
            if (allocation.reservation.id == reservation_id) {
                return &allocation;
            }
        }
    }
    return nullptr;
}

void StagingPoolPlanner::release(Allocation allocation) {
    Block& block = blocks_[allocation.reservation.block_index];
    reserved_byte_size_ -= allocation.reservation.byte_size;
    block.free_ranges.push_back({
        .offset = allocation.reservation.offset,
        .byte_size = allocation.reservation.byte_size,
    });
    std::sort(
        block.free_ranges.begin(), block.free_ranges.end(),
        [](const FreeRange& left, const FreeRange& right) { return left.offset < right.offset; });
    std::vector<FreeRange> merged;
    merged.reserve(block.free_ranges.size());
    for (const FreeRange& range : block.free_ranges) {
        if (!merged.empty() && merged.back().offset + merged.back().byte_size == range.offset) {
            merged.back().byte_size += range.byte_size;
        } else {
            merged.push_back(range);
        }
    }
    block.free_ranges = std::move(merged);
}

void StagingPoolPlanner::add_block(VkDeviceSize byte_size) {
    if (byte_size == 0 || byte_size > config_.max_total_byte_size - total_capacity_byte_size_) {
        throw std::runtime_error("staging pool block exceeds its capacity cap");
    }
    blocks_.push_back({
        .byte_size = byte_size,
        .free_ranges = {{.offset = 0, .byte_size = byte_size}},
        .allocations = {},
    });
    total_capacity_byte_size_ += byte_size;
}

} // namespace cubey::vulkan::detail
