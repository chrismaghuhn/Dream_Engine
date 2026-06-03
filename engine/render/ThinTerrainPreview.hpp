#pragma once

#include "engine/render/GpuMeshPool.hpp"
#include "engine/render/MeshUploadQueue.hpp"
#include "engine/render/WorldRenderSnapshot.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/GreedyMesher.hpp"
#include "engine/world/SectionIndexing.hpp"
#include "engine/world/WorldConfig.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <flecs.h>
#include <glm/glm.hpp>

namespace engine {

class ThinTerrainPreview {
public:
    void init(flecs::world& world, ChunkStore& store, const WorldConfig& world_config);
    void build_cpu_meshes();
    void ensure_gpu_slots(GpuMeshPool& mesh_pool, std::uint64_t frame_index);
    void queue_uploads(MeshUploadQueue& upload_queue);
    void fill_snapshot(WorldRenderSnapshot& snapshot, const glm::vec3& render_origin);

    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] bool uploads_queued() const { return uploads_queued_; }

private:
    ChunkStore* store_ = nullptr;
    struct SectionMesh {
        std::uint8_t section_index = 0;
        std::uint32_t opaque_slot_id = 0;
        std::uint32_t water_slot_id = 0;
        std::uint32_t opaque_index_count = 0;
        std::uint32_t water_index_count = 0;
        std::vector<TerrainVertex> opaque_vertices;
        std::vector<std::uint32_t> opaque_indices;
        std::vector<TerrainVertex> water_vertices;
        std::vector<std::uint32_t> water_indices;
    };

    ChunkCoord coord_{0, 0, 0};
    std::array<SectionMesh, 8> sections_{};
    bool meshes_built_ = false;
    bool slots_allocated_ = false;
    bool uploads_queued_ = false;
    bool ready_ = false;
};

} // namespace engine
