#include "engine/render/StagingRing.hpp"

#include <algorithm>

namespace engine {

StagingRing::StagingRing(std::uint32_t frames_in_flight, std::size_t chunk_mesh_cpu_ram)
    : frames_in_flight_(frames_in_flight > 0 ? frames_in_flight : 1)
    , slot_size_(std::max<std::size_t>(1, chunk_mesh_cpu_ram / frames_in_flight_))
    , slot_used_(frames_in_flight_, 0) {}

std::uint32_t StagingRing::ring_index(std::uint64_t frame_index) const {
    return static_cast<std::uint32_t>(frame_index % frames_in_flight_);
}

std::size_t StagingRing::bytes_used(std::uint32_t ring_index) const {
    return slot_used_[ring_index];
}

void StagingRing::reset_slot(std::uint32_t ring_index) {
    slot_used_[ring_index] = 0;
}

std::size_t StagingRing::try_enqueue(std::uint64_t frame_index, StagingUpload upload) {
    if (upload.size == 0) {
        return 0;
    }

    const std::uint32_t ring = ring_index(frame_index);
    const std::size_t remaining = slot_size_ > slot_used_[ring] ? slot_size_ - slot_used_[ring] : 0;

    if (remaining == 0) {
        pending_uploads_.push_back(upload);
        return 0;
    }

    if (upload.size <= remaining) {
        slot_used_[ring] += upload.size;
        return upload.size;
    }

    StagingUpload overflow{
        .id = upload.id,
        .size = upload.size - remaining,
    };
    slot_used_[ring] = slot_size_;
    pending_uploads_.push_back(overflow);
    return remaining;
}

} // namespace engine
