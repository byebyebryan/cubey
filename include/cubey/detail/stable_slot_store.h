#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <vector>

namespace cubey::detail {

struct StableSlotId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool is_null() const noexcept {
        return index == 0;
    }

    explicit operator bool() const noexcept {
        return !is_null();
    }

    friend bool operator==(StableSlotId lhs, StableSlotId rhs) = default;
};

struct StableSlotIdHash {
    [[nodiscard]] std::size_t operator()(StableSlotId id) const noexcept {
        return (static_cast<std::size_t>(id.index) * 16'777'619U) ^
               static_cast<std::size_t>(id.generation);
    }
};

template <typename T, std::size_t PageSize = 64> class StableSlotStore {
  public:
    static_assert(PageSize > 0);

    [[nodiscard]] StableSlotId create(const T& value = T{}) {
        std::uint32_t index = 0;
        if (!free_indices_.empty()) {
            index = free_indices_.front();
            free_indices_.pop_front();
        } else {
            index = next_index_;
            ++next_index_;
            ensure_page_for(index);
        }

        Slot& slot = slot_at(index);
        if (slot.generation == 0) {
            slot.generation = 1;
        }
        slot.state = SlotState::Alive;
        slot.retire_epoch = 0;
        slot.value = value;
        return StableSlotId{.index = index, .generation = slot.generation};
    }

    void destroy(StableSlotId id, std::uint64_t retire_epoch) {
        Slot& slot = validated_slot(id);
        ++slot.generation;
        slot.state = SlotState::PendingDestroy;
        slot.retire_epoch = retire_epoch;
    }

    [[nodiscard]] T& get(StableSlotId id) {
        return validated_slot(id).value;
    }

    [[nodiscard]] const T& get(StableSlotId id) const {
        return validated_slot(id).value;
    }

    [[nodiscard]] bool is_alive(StableSlotId id) const {
        if (!id || id.index >= next_index_) {
            return false;
        }
        const Slot& slot = slot_at(id.index);
        return slot.state == SlotState::Alive && slot.generation == id.generation;
    }

    [[nodiscard]] std::vector<StableSlotId> active_instances() const {
        std::vector<StableSlotId> result;
        for (std::uint32_t index = 1; index < next_index_; ++index) {
            const Slot& slot = slot_at(index);
            if (slot.state == SlotState::Alive) {
                result.push_back(StableSlotId{.index = index, .generation = slot.generation});
            }
        }
        return result;
    }

    [[nodiscard]] std::size_t retire_destroyed_up_to(std::uint64_t epoch) {
        std::size_t retired = 0;
        for (std::uint32_t index = 1; index < next_index_; ++index) {
            Slot& slot = slot_at(index);
            if (slot.state == SlotState::PendingDestroy && slot.retire_epoch <= epoch) {
                slot.state = SlotState::Free;
                slot.retire_epoch = 0;
                free_indices_.push_back(index);
                ++retired;
            }
        }
        return retired;
    }

  private:
    enum class SlotState {
        Free,
        Alive,
        PendingDestroy,
    };

    struct Slot {
        T value{};
        std::uint32_t generation = 0;
        SlotState state = SlotState::Free;
        std::uint64_t retire_epoch = 0;
    };

    using Page = std::array<Slot, PageSize>;

    void ensure_page_for(std::uint32_t index) {
        const std::size_t page_index = page_index_for(index);
        while (pages_.size() <= page_index) {
            pages_.push_back(std::make_unique<Page>());
        }
    }

    [[nodiscard]] Slot& validated_slot(StableSlotId id) {
        if (!is_alive(id)) {
            throw std::runtime_error("stable slot handle is invalid");
        }
        return slot_at(id.index);
    }

    [[nodiscard]] const Slot& validated_slot(StableSlotId id) const {
        if (!is_alive(id)) {
            throw std::runtime_error("stable slot handle is invalid");
        }
        return slot_at(id.index);
    }

    [[nodiscard]] static std::size_t page_index_for(std::uint32_t index) {
        return static_cast<std::size_t>(index - 1U) / PageSize;
    }

    [[nodiscard]] static std::size_t slot_index_for(std::uint32_t index) {
        return static_cast<std::size_t>(index - 1U) % PageSize;
    }

    [[nodiscard]] Slot& slot_at(std::uint32_t index) {
        return (*pages_[page_index_for(index)])[slot_index_for(index)];
    }

    [[nodiscard]] const Slot& slot_at(std::uint32_t index) const {
        return (*pages_[page_index_for(index)])[slot_index_for(index)];
    }

    std::vector<std::unique_ptr<Page>> pages_{};
    std::deque<std::uint32_t> free_indices_{};
    std::uint32_t next_index_ = 1;
};

} // namespace cubey::detail
