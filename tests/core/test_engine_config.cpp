#include <catch2/catch_test_macros.hpp>

#include "engine/core/EngineConfig.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/world/TerrainLod.hpp"

TEST_CASE("finalize_cpu leaves gpu_mesh_vram at zero") {
    engine::EngineConfig cfg;
    const auto cpu = engine::HardwareProbe::run_cpu();
    cfg.finalize_cpu(cpu);
    REQUIRE(cfg.memory().gpu_mesh_vram == 0);
}

TEST_CASE("occlusion_grid_radius_chunks is positive after finalize_cpu") {
    engine::EngineConfig cfg;
    cfg.finalize_cpu(engine::HardwareProbe::run_cpu());
    REQUIRE(cfg.occlusion_grid_radius_chunks() > 0);
    const int expected = cfg.cpu_hardware().physical_cores >= 6 ? 48 : 32;
    REQUIRE(cfg.occlusion_grid_radius_chunks() == expected);
}

TEST_CASE("terrain_lod_config_from_preset maps Low and High thresholds") {
    const engine::TerrainLodConfig low =
        engine::terrain_lod_config_from_preset(engine::RenderPreset::Low);
    REQUIRE(low.lod0_far_blocks == 64.f);
    REQUIRE(low.lod1_far_blocks == 384.f);

    const engine::TerrainLodConfig high =
        engine::terrain_lod_config_from_preset(engine::RenderPreset::High);
    REQUIRE(high.lod0_far_blocks == 128.f);
    REQUIRE(high.lod1_far_blocks == 1024.f);

    const engine::TerrainLodConfig medium =
        engine::terrain_lod_config_from_preset(engine::RenderPreset::Medium);
    REQUIRE(medium.lod0_far_blocks == 96.f);
    REQUIRE(medium.lod1_far_blocks == 512.f);
}

TEST_CASE("finalize_gpu applies preset terrain lod config") {
    engine::EngineConfig cfg;
    cfg.finalize_cpu(engine::HardwareProbe::run_cpu());

    engine::GpuCaps gpu{};
    gpu.vram_bytes = 8ULL * 1024 * 1024 * 1024;
    gpu.discrete_gpu = true;
    cfg.finalize_gpu(gpu, engine::RenderPreset::High);

    REQUIRE(cfg.terrain_lod().lod1_far_blocks == 1024.f);
    REQUIRE(cfg.terrain_lod().lod0_far_blocks == 128.f);
}

TEST_CASE("finalize_gpu sets gpu_mesh_vram") {
    engine::EngineConfig cfg;
    cfg.finalize_cpu(engine::HardwareProbe::run_cpu());

    engine::GpuCaps gpu{};
    gpu.vram_bytes = 8ULL * 1024 * 1024 * 1024;
    gpu.discrete_gpu = true;
    cfg.finalize_gpu(gpu);

    REQUIRE(cfg.memory().gpu_mesh_vram > 0);
}

TEST_CASE("finalize_cpu derives destruction limits from RAM") {
    engine::EngineConfig cfg;
    engine::CpuHardware cpu{};
    cpu.ram_bytes = 8ULL * 1024 * 1024 * 1024;
    cpu.physical_cores = 4;
    cfg.finalize_cpu(cpu);

    REQUIRE(cfg.destruction().max_active_debris >= 64);
    REQUIRE(cfg.destruction().max_fracture_depth >= 2);
}
