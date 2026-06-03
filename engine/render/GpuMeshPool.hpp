#pragma once

#include "engine/render/GpuDeferredFreeQueue.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <volk.h>

namespace engine {

enum class MeshBucket : std::uint8_t { B4K = 0, B16K, B64K, B256K, Count };

[[nodiscard]] MeshBucket pick_mesh_bucket(std::size_t vertex_bytes, std::size_t index_bytes);
[[nodiscard]] std::size_t mesh_bucket_vertex_capacity(MeshBucket bucket);
[[nodiscard]] std::size_t mesh_bucket_index_capacity(MeshBucket bucket);

struct GpuMeshSlot {
    std::uint32_t slot_id = 0;
    MeshBucket bucket = MeshBucket::B4K;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize memory_size = 0;
    std::size_t vertex_capacity = 0;
    std::size_t index_capacity = 0;
    bool live = false;
};

[[nodiscard]] inline std::uint32_t clamp_index_count(const GpuMeshSlot* slot, std::uint32_t index_count) {
    if (slot == nullptr || slot->index_capacity < sizeof(std::uint32_t)) {
        return 0;
    }
    const std::uint32_t capacity_indices =
        static_cast<std::uint32_t>(slot->index_capacity / sizeof(std::uint32_t));
    return std::min(index_count, capacity_indices);
}

class GpuMeshPool {
public:
    GpuMeshPool() = default;

    void init(VkDevice device,
              VkPhysicalDevice physical_device,
              std::size_t bytes_budget,
              GpuDeferredFreeQueue* deferred_free);
    void shutdown();

    [[nodiscard]] std::uint32_t allocate(std::size_t vertex_bytes, std::size_t index_bytes);
    [[nodiscard]] std::uint32_t regrow(std::uint32_t old_slot_id,
                                       std::size_t vertex_bytes,
                                       std::size_t index_bytes,
                                       std::uint32_t last_submit_snapshot_slot);
    void release_immediate(std::uint32_t slot_id);

    [[nodiscard]] const GpuMeshSlot* slot(std::uint32_t slot_id) const;
    [[nodiscard]] bool is_live(std::uint32_t slot_id) const;
    [[nodiscard]] std::size_t bytes_used() const { return bytes_used_; }
    [[nodiscard]] std::size_t bytes_budget() const { return bytes_budget_; }
    [[nodiscard]] std::size_t live_slot_count() const;
    void set_bytes_budget(std::size_t bytes_budget) { bytes_budget_ = bytes_budget; }

    static constexpr std::size_t kMaxLiveSlots = 1024;

private:
    [[nodiscard]] GpuMeshSlot* find_slot(std::uint32_t slot_id);
    [[nodiscard]] const GpuMeshSlot* find_slot(std::uint32_t slot_id) const;
    [[nodiscard]] std::uint32_t allocate_sized(std::size_t vertex_bytes, std::size_t index_bytes);
    [[nodiscard]] std::uint32_t allocate_bucket(MeshBucket bucket);
    [[nodiscard]] bool create_bucket_gpu_resources(GpuMeshSlot& slot, MeshBucket bucket);
    void destroy_slot_resources(GpuMeshSlot& slot);
    void return_slot_to_freelist(GpuMeshSlot& slot);

    std::size_t bytes_budget_ = 0;
    std::size_t bytes_used_ = 0;
    std::uint32_t next_slot_id_ = 1;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    GpuDeferredFreeQueue* deferred_free_ = nullptr;
    std::vector<GpuMeshSlot> slots_;
    std::array<std::vector<std::uint32_t>, static_cast<std::size_t>(MeshBucket::Count)> free_lists_{};
};

} // namespace engine
