#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace engine {

struct StagingUpload {
    std::uint32_t id = 0;
    std::size_t size = 0;
};

class StagingRing {
public:
    StagingRing(std::uint32_t frames_in_flight, std::size_t chunk_mesh_cpu_ram);

    [[nodiscard]] std::uint32_t frames_in_flight() const { return frames_in_flight_; }
    [[nodiscard]] std::size_t slot_size() const { return slot_size_; }
    [[nodiscard]] std::size_t bytes_used(std::uint32_t ring_index) const;
    [[nodiscard]] std::size_t pending_upload_count() const { return pending_uploads_.size(); }
    [[nodiscard]] const std::deque<StagingUpload>& pending_uploads() const { return pending_uploads_; }

    void reset_slot(std::uint32_t ring_index);
    std::size_t try_enqueue(std::uint64_t frame_index, StagingUpload upload);

private:
    [[nodiscard]] std::uint32_t ring_index(std::uint64_t frame_index) const;

    std::uint32_t frames_in_flight_ = 0;
    std::size_t slot_size_ = 0;
    std::vector<std::size_t> slot_used_;
    std::deque<StagingUpload> pending_uploads_;
};

} // namespace engine
