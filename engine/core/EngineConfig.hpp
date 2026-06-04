#pragma once

#include "engine/core/HardwareProbe.hpp"
#include "engine/core/MemoryBudget.hpp"
#include "engine/render/SectionVisibility.hpp"
#include "engine/world/StreamingConfig.hpp"
#include "engine/world/TerrainLod.hpp"
#include "engine/world/WorldConfig.hpp"

#include <cstdint>
#include <string>

namespace engine {

struct ThreadConfig {
    int worker_threads = 0;
    int io_threads = 0;
    int meshing_threads = 0;
};

struct DestructionConfig {
    int max_active_debris = 0;
    int max_fracture_depth = 0;
    float debris_despawn_radius = 0.f;
};

enum class RenderPreset { Low, Medium, High };

struct GpuCaps {
    size_t vram_bytes = 0;
    bool multi_draw_indirect = false;
    bool discrete_gpu = false;
    bool descriptor_indexing = false;
    uint32_t graphics_queue_family = 0;
    uint32_t max_memory_allocation_count = 0;
    size_t max_storage_buffer_range = 0;
    size_t min_uniform_buffer_offset_alignment = 0;
    size_t non_coherent_atom_size = 0;
};

class EngineConfig {
public:
    void load_toml(const std::string& path);
    void finalize_cpu(const CpuHardware& cpu);
    void finalize_gpu(const GpuCaps& gpu, RenderPreset preset = RenderPreset::Medium);

    [[nodiscard]] int occlusion_grid_radius_chunks() const;
    [[nodiscard]] const ThreadConfig& threads() const { return threads_; }
    [[nodiscard]] const MemoryBudget& memory() const { return memory_; }
    [[nodiscard]] const WorldConfig& world() const { return world_; }
    [[nodiscard]] const StreamingConfig& streaming() const { return streaming_; }
    [[nodiscard]] bool thin_terrain_preview() const { return thin_terrain_preview_; }
    [[nodiscard]] bool creative_place() const { return creative_place_; }
    [[nodiscard]] const CpuHardware& cpu_hardware() const { return cpu_; }
    [[nodiscard]] RenderPreset render_preset() const { return render_preset_; }
    [[nodiscard]] const TerrainLodConfig& terrain_lod() const { return terrain_lod_config_; }
    [[nodiscard]] const TerrainOcclusionConfig& terrain_occlusion() const {
        return terrain_occlusion_config_;
    }
    [[nodiscard]] const DestructionConfig& destruction() const { return destruction_; }

private:
    ThreadConfig threads_{};
    MemoryBudget memory_{};
    WorldConfig world_{};
    StreamingConfig streaming_{};
    int streaming_horizontal_override_ = 0;
    int streaming_vertical_override_ = 0;
    int streaming_max_chunks_override_ = 0;
    int streaming_load_budget_override_ = 0;
    bool thin_terrain_preview_ = false;
    bool creative_place_ = false;
    CpuHardware cpu_{};
    DestructionConfig destruction_{};
    int destruction_max_active_debris_override_ = 0;
    int destruction_max_fracture_depth_override_ = 0;
    float destruction_debris_despawn_radius_override_ = 0.f;
    RenderPreset render_preset_ = RenderPreset::Medium;
    TerrainLodConfig terrain_lod_config_{};
    TerrainOcclusionConfig terrain_occlusion_config_{};
    bool cpu_finalized_ = false;
};

[[nodiscard]] TerrainOcclusionConfig terrain_occlusion_config_from_preset(RenderPreset preset);

ThreadConfig thread_config_from_hardware(const CpuHardware& cpu, const ThreadConfig& overrides);
DestructionConfig destruction_config_from_hardware(const CpuHardware& cpu);

} // namespace engine
