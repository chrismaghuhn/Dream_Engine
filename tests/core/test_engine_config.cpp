#include <catch2/catch_test_macros.hpp>

#include "engine/core/EngineConfig.hpp"
#include "engine/core/HardwareProbe.hpp"

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

TEST_CASE("finalize_gpu sets gpu_mesh_vram") {
    engine::EngineConfig cfg;
    cfg.finalize_cpu(engine::HardwareProbe::run_cpu());

    engine::GpuCaps gpu{};
    gpu.vram_bytes = 8ULL * 1024 * 1024 * 1024;
    gpu.discrete_gpu = true;
    cfg.finalize_gpu(gpu);

    REQUIRE(cfg.memory().gpu_mesh_vram > 0);
}
