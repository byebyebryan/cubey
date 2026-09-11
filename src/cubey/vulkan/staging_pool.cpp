#include <cubey/vulkan/staging_pool.h>

#include <cubey/vulkan/gpu_runtime.h>

#include <stdexcept>

namespace cubey::vulkan {

GpuStagingPool::GpuStagingPool(GpuStagingPoolConfig config) : planner_(planner_config(config)) {}

GpuStagingPool::~GpuStagingPool() = default;

void GpuStagingPool::initialize(GpuOwnerContext& context) {
    context.require_owner_thread("GPU staging pool initialization requires the GPU owner thread");
    try {
        ensure_physical_block(context, 0U);
        initialized_ = true;
    } catch (...) {
        blocks_.clear();
        initialized_ = false;
        throw;
    }
}

std::optional<GpuStagingReservation> GpuStagingPool::try_reserve(GpuOwnerContext& context,
                                                                 VkDeviceSize byte_size,
                                                                 VkDeviceSize alignment) {
    context.require_owner_thread("GPU staging pool allocation requires the GPU owner thread");
    if (!initialized_) {
        throw std::runtime_error("GPU staging pool must be initialized before use");
    }
    static_cast<void>(reclaim(context));
    std::optional<detail::StagingPoolReservationPlan> plan =
        planner_.try_reserve(byte_size, alignment);
    if (!plan.has_value()) {
        return std::nullopt;
    }
    try {
        ensure_physical_block(context, plan->block_index);
    } catch (...) {
        planner_.cancel(plan->id);
        throw;
    }
    Block& block = blocks_[plan->block_index];
    GpuStagingReservation reservation;
    reservation.buffer = block.buffer->handle();
    reservation.mapped = block.mapped + static_cast<std::size_t>(plan->offset);
    reservation.offset = plan->offset;
    reservation.byte_size = plan->byte_size;
    reservation.plan = std::move(plan.value());
    return reservation;
}

void GpuStagingPool::commit(GpuOwnerContext& context, GpuStagingReservation& reservation,
                            GpuSubmissionTicket retirement_ticket) {
    context.require_owner_thread("GPU staging pool commit requires the GPU owner thread");
    if (reservation.plan.id == 0) {
        throw std::runtime_error("GPU staging reservation is not active");
    }
    planner_.commit(reservation.plan.id, retirement_ticket);
    reservation.plan = {};
}

void GpuStagingPool::commit_many(GpuOwnerContext& context,
                                 std::span<GpuStagingReservation> reservations,
                                 GpuSubmissionTicket retirement_ticket) {
    context.require_owner_thread("GPU staging pool commit requires the GPU owner thread");
    if (reservations.empty()) {
        throw std::runtime_error("GPU staging pool commit requires reservations");
    }

    std::vector<std::uint64_t> reservation_ids;
    reservation_ids.reserve(reservations.size());
    for (const GpuStagingReservation& reservation : reservations) {
        if (reservation.plan.id == 0) {
            throw std::runtime_error("GPU staging reservation is not active");
        }
        reservation_ids.push_back(reservation.plan.id);
    }
    planner_.commit_many(reservation_ids, retirement_ticket);
    for (GpuStagingReservation& reservation : reservations) {
        reservation.buffer = VK_NULL_HANDLE;
        reservation.mapped = nullptr;
        reservation.offset = 0;
        reservation.byte_size = 0;
        reservation.plan = {};
    }
}

void GpuStagingPool::cancel(GpuOwnerContext& context, GpuStagingReservation& reservation) {
    context.require_owner_thread("GPU staging pool cancellation requires the GPU owner thread");
    if (reservation.plan.id == 0) {
        return;
    }
    planner_.cancel(reservation.plan.id);
    reservation.plan = {};
}

std::size_t GpuStagingPool::reclaim(GpuOwnerContext& context) {
    context.require_owner_thread("GPU staging pool reclamation requires the GPU owner thread");
    return planner_.reclaim(context.completed_submission());
}

void GpuStagingPool::shutdown_on_owner_thread(GpuOwnerContext& context) {
    context.require_owner_thread("GPU staging pool shutdown requires the GPU owner thread");
    static_cast<void>(planner_.reclaim(context.completed_submission()));
    blocks_.clear();
    initialized_ = false;
}

GpuStagingPoolStats GpuStagingPool::stats() const noexcept {
    return {
        .block_count = planner_.block_count(),
        .capacity_byte_size = planner_.total_capacity_byte_size(),
        .reserved_byte_size = planner_.reserved_byte_size(),
    };
}

void GpuStagingPool::ensure_physical_block(GpuOwnerContext& context, std::size_t block_index) {
    while (blocks_.size() <= block_index) {
        blocks_.push_back({});
    }
    Block& block = blocks_[block_index];
    if (block.buffer.has_value()) {
        return;
    }
    try {
        block.buffer.emplace(context.device(),
                             staging_buffer_config(planner_.block_byte_size(block_index)));
        block.mapped = block.buffer->map_persistent();
    } catch (...) {
        block.buffer.reset();
        block.mapped = nullptr;
        throw;
    }
}

detail::StagingPoolPlanConfig GpuStagingPool::planner_config(GpuStagingPoolConfig config) noexcept {
    return {
        .initial_block_byte_size = config.initial_block_byte_size,
        .growth_block_byte_size = config.growth_block_byte_size,
        .max_total_byte_size = config.max_total_byte_size,
    };
}

} // namespace cubey::vulkan
