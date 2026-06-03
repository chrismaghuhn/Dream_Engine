#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

namespace engine {

class SnapshotRing {
public:
    static constexpr std::uint32_t snapshot_count(std::uint32_t frames_in_flight) {
        return frames_in_flight + 1;
    }

    explicit SnapshotRing(std::uint32_t frames_in_flight)
        : slot_count_(snapshot_count(frames_in_flight))
        , slot_signaled_(slot_count_, true) {}

    [[nodiscard]] std::uint32_t slot_count() const { return slot_count_; }

    [[nodiscard]] std::uint32_t pick_write_slot() const {
        for (std::uint32_t slot = 0; slot < slot_count_; ++slot) {
            if (slot_signaled_[slot]) {
                return slot;
            }
        }
        assert(false && "no signaled snapshot slot available");
        return 0;
    }

    void mark_submitted(std::uint32_t slot) {
        assert(slot < slot_count_);
        slot_signaled_[slot] = false;
    }

    void mark_gpu_complete(std::uint32_t slot) {
        assert(slot < slot_count_);
        slot_signaled_[slot] = true;
    }

private:
    std::uint32_t slot_count_;
    std::vector<bool> slot_signaled_;
};

} // namespace engine
