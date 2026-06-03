#pragma once

#include <cstddef>

namespace engine {

struct MemoryBudget {
    size_t chunk_voxel_ram = 0;
    size_t chunk_mesh_cpu_ram = 0;
    size_t gpu_mesh_vram = 0;
    size_t io_cache_ram = 0;
};

MemoryBudget finalize_cpu_budget(const struct CpuHardware& cpu);

void finalize_gpu_budget(MemoryBudget& mem, const struct GpuCaps& gpu);

} // namespace engine
