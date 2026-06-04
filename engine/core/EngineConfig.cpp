#include "engine/core/EngineConfig.hpp"

#include "engine/world/StreamingConfig.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

namespace engine {

namespace {

int read_int_or_default(const toml::table& table, const char* key, int default_value) {
    if (const auto value = table[key].value<int>()) {
        return *value;
    }
    return default_value;
}

bool read_bool_or_default(const toml::table& table, const char* key, bool default_value) {
    if (const auto value = table[key].value<bool>()) {
        return *value;
    }
    return default_value;
}

float read_float_or_default(const toml::table& table, const char* key, float default_value) {
    if (const auto value = table[key].value<float>()) {
        return *value;
    }
    if (const auto value = table[key].value<double>()) {
        return static_cast<float>(*value);
    }
    return default_value;
}

RenderPreset render_preset_from_gpu(const GpuCaps& gpu) {
    constexpr size_t k8GiB = 8ULL * 1024 * 1024 * 1024;
    constexpr size_t k4GiB = 4ULL * 1024 * 1024 * 1024;
    if (gpu.vram_bytes >= k8GiB && gpu.discrete_gpu) {
        return RenderPreset::High;
    }
    if (gpu.vram_bytes >= k4GiB) {
        return RenderPreset::Medium;
    }
    return RenderPreset::Low;
}

void apply_streaming_toml_overrides(
    StreamingConfig& streaming,
    int horizontal_override,
    int vertical_override,
    int max_chunks_override,
    int load_budget_override) {
    if (horizontal_override > 0) {
        streaming.horizontal_radius_chunks = horizontal_override;
    }
    if (vertical_override > 0) {
        streaming.vertical_radius_chunks = vertical_override;
    }
    if (max_chunks_override > 0) {
        streaming.max_loaded_chunks = max_chunks_override;
    }
    if (load_budget_override > 0) {
        streaming.max_chunks_load_per_update = load_budget_override;
    }
}

} // namespace

MemoryBudget finalize_cpu_budget(const CpuHardware& cpu) {
    const size_t ram = cpu.ram_bytes > 0 ? cpu.ram_bytes : (8ULL * 1024 * 1024 * 1024);
    return MemoryBudget{
        .chunk_voxel_ram = ram * 10 / 100,
        .chunk_mesh_cpu_ram = ram * 3 / 100,
        .gpu_mesh_vram = 0,
        .io_cache_ram = ram * 2 / 100,
    };
}

void finalize_gpu_budget(MemoryBudget& mem, const GpuCaps& gpu) {
    const RenderPreset preset = render_preset_from_gpu(gpu);
    size_t pct = 35;
    if (preset == RenderPreset::High) {
        pct = 45;
    } else if (preset == RenderPreset::Low) {
        pct = 25;
    }

    mem.gpu_mesh_vram = gpu.vram_bytes * pct / 100;

    constexpr size_t kReserve = 512ULL * 1024 * 1024;
    if (gpu.vram_bytes > kReserve && mem.gpu_mesh_vram > gpu.vram_bytes - kReserve) {
        mem.gpu_mesh_vram = gpu.vram_bytes - kReserve;
    } else if (mem.gpu_mesh_vram > gpu.vram_bytes) {
        mem.gpu_mesh_vram = gpu.vram_bytes / 4;
    }
}

DestructionConfig destruction_config_from_hardware(const CpuHardware& cpu) {
    const size_t ram = cpu.ram_bytes > 0 ? cpu.ram_bytes : (8ULL * 1024 * 1024 * 1024);
    constexpr size_t kDebrisBytesEst = 8 * 1024;
    const int max_debris = static_cast<int>(std::clamp(ram / kDebrisBytesEst, 64ULL, 4096ULL));
    const int max_depth =
        ram >= 16ULL * 1024 * 1024 * 1024 ? 4 : ram >= 8ULL * 1024 * 1024 * 1024 ? 3 : 2;
    return DestructionConfig{
        .max_active_debris = max_debris,
        .max_fracture_depth = max_depth,
    };
}

ThreadConfig thread_config_from_hardware(const CpuHardware& cpu, const ThreadConfig& overrides) {
    ThreadConfig out{};
    out.worker_threads =
        overrides.worker_threads > 0 ? overrides.worker_threads : std::max(1, cpu.physical_cores - 2);
    out.io_threads = overrides.io_threads > 0 ? overrides.io_threads : (cpu.has_ssd ? 2 : 1);
    const int meshing_auto = std::min(out.worker_threads, 4);
    out.meshing_threads =
        overrides.meshing_threads > 0 ? overrides.meshing_threads : meshing_auto;
    return out;
}

void EngineConfig::load_toml(const std::string& path) {
    const toml::table table = toml::parse_file(path);

    if (const auto* engine = table["engine"].as_table()) {
        if (const auto* threads = (*engine)["threads"].as_table()) {
            threads_.worker_threads = read_int_or_default(*threads, "worker", 0);
            threads_.io_threads = read_int_or_default(*threads, "io", 0);
            threads_.meshing_threads = read_int_or_default(*threads, "meshing", 0);
        }
    }

    if (const auto* world = table["world"].as_table()) {
        world_.chunk_height_min = read_int_or_default(*world, "chunk_height_min", world_.chunk_height_min);
        world_.chunk_height_max = read_int_or_default(*world, "chunk_height_max", world_.chunk_height_max);
        world_.finite_bounds = read_bool_or_default(*world, "finite_bounds", world_.finite_bounds);
        world_.sea_level = read_int_or_default(*world, "sea_level", world_.sea_level);
        if (const auto seed = (*world)["world_seed"].value<int64_t>()) {
            world_.world_seed = static_cast<uint64_t>(*seed);
        }
        world_.rebase_radius = read_float_or_default(*world, "rebase_radius", world_.rebase_radius);
        world_.player_reach = read_float_or_default(*world, "player_reach", world_.player_reach);
    }

    if (const auto* engine = table["engine"].as_table()) {
        if (const auto* streaming = (*engine)["streaming"].as_table()) {
            streaming_horizontal_override_ =
                read_int_or_default(*streaming, "horizontal_radius_chunks", 0);
            streaming_vertical_override_ =
                read_int_or_default(*streaming, "vertical_radius_chunks", 0);
            streaming_max_chunks_override_ =
                read_int_or_default(*streaming, "max_loaded_chunks", 0);
            streaming_load_budget_override_ =
                read_int_or_default(*streaming, "max_chunks_load_per_update", 0);
        }
    }

    if (const auto* render = table["render"].as_table()) {
        thin_terrain_preview_ = read_bool_or_default(*render, "thin_terrain_preview", false);
    }

    if (const auto* debug = table["debug"].as_table()) {
        creative_place_ = read_bool_or_default(*debug, "creative_place", false);
    }

    if (const auto* engine = table["engine"].as_table()) {
        if (const auto* destruction = (*engine)["destruction"].as_table()) {
            destruction_max_active_debris_override_ =
                read_int_or_default(*destruction, "max_active_debris", 0);
            destruction_max_fracture_depth_override_ =
                read_int_or_default(*destruction, "max_fracture_depth", 0);
            destruction_debris_despawn_radius_override_ =
                read_float_or_default(*destruction, "debris_despawn_radius", 0.f);
        }
    }
}

void EngineConfig::finalize_cpu(const CpuHardware& cpu) {
    cpu_ = cpu;
    const ThreadConfig overrides = threads_;
    threads_ = thread_config_from_hardware(cpu, overrides);
    memory_ = finalize_cpu_budget(cpu);
    memory_.gpu_mesh_vram = 0;
    streaming_ = streaming_config_from_hardware(memory_, world_, RenderPreset::Medium);
    apply_streaming_toml_overrides(
        streaming_,
        streaming_horizontal_override_,
        streaming_vertical_override_,
        streaming_max_chunks_override_,
        streaming_load_budget_override_);
    destruction_ = destruction_config_from_hardware(cpu);
    if (destruction_max_active_debris_override_ > 0) {
        destruction_.max_active_debris = destruction_max_active_debris_override_;
    }
    if (destruction_max_fracture_depth_override_ > 0) {
        destruction_.max_fracture_depth = destruction_max_fracture_depth_override_;
    }
    if (destruction_debris_despawn_radius_override_ > 0.f) {
        destruction_.debris_despawn_radius = destruction_debris_despawn_radius_override_;
    }
    cpu_finalized_ = true;
}

void EngineConfig::finalize_gpu(const GpuCaps& gpu, RenderPreset preset) {
    render_preset_ = preset == RenderPreset::Medium ? render_preset_from_gpu(gpu) : preset;
    finalize_gpu_budget(memory_, gpu);
    terrain_lod_config_ = terrain_lod_config_from_preset(render_preset_);
    streaming_ = streaming_config_from_hardware(memory_, world_, render_preset_);
    apply_streaming_toml_overrides(
        streaming_,
        streaming_horizontal_override_,
        streaming_vertical_override_,
        streaming_max_chunks_override_,
        streaming_load_budget_override_);
}

int EngineConfig::occlusion_grid_radius_chunks() const {
    if (!cpu_finalized_) {
        return 32;
    }
    return cpu_.physical_cores >= 6 ? 48 : 32;
}

} // namespace engine
