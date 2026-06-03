#include "engine/Engine.hpp"

#include "engine/core/CrashHandlerWin32.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/core/Log.hpp"

#include <spdlog/spdlog.h>

#include <string>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

namespace engine {

Engine::~Engine() {
    shutdown();
}

bool Engine::startup() {
    if (started_) {
        return true;
    }

    // Step 1: CrashHandler
    crash_handler_install();

    // Step 2: Logging
    log_init();

    // Step 3: HardwareProbe
    const CpuHardware cpu = HardwareProbe::run_cpu();
    SPDLOG_INFO("CPU probe: {} physical / {} logical cores, {} MiB RAM",
                cpu.physical_cores,
                cpu.logical_cores,
                cpu.ram_bytes / (1024 * 1024));

    // Step 4: EngineConfig load + finalize_cpu
    const std::string config_path = std::string(ENGINE_SOURCE_DIR) + "/assets/default.toml";
    config_.load_toml(config_path);
    config_.finalize_cpu(cpu);
    SPDLOG_INFO("EngineConfig loaded from {}", config_path);

    // Step 5: Tracy (optional) — skipped until ENGINE_TRACY is wired

    // Step 6: Flecs world + empty module
    world_ = flecs::world();
    world_.module<VoxelEngineModule>();

    // Step 7: JobSystem
    jobs_.init(config_.threads());

    // Step 8: Platform / GLFW window
    if (!platform_.init(1280, 720, "VoxelEngine")) {
        SPDLOG_ERROR("Failed to create GLFW window");
        jobs_.shutdown();
        world_ = flecs::world{};
        return false;
    }

    // Step 9: Renderer (Vulkan instance/device/swapchain) -> GpuCaps
    if (!renderer_.init(platform_)) {
        SPDLOG_ERROR("Failed to initialize Renderer");
        platform_.shutdown();
        jobs_.shutdown();
        world_ = flecs::world{};
        return false;
    }

    // Step 9b: finalize_gpu before ChunkStore or other GPU-budget consumers
    config_.finalize_gpu(renderer_.gpu_caps());
    SPDLOG_INFO("GPU finalize: {} MiB mesh VRAM budget, preset {}",
                config_.memory().gpu_mesh_vram / (1024 * 1024),
                static_cast<int>(config_.render_preset()));

    started_ = true;
    SPDLOG_INFO("Engine startup complete (steps 1-9b)");
    return true;
}

void Engine::shutdown() {
    if (!started_) {
        return;
    }

    // Reverse init order: 9 → 1
    renderer_.shutdown();
    platform_.shutdown();
    jobs_.shutdown();
    world_ = flecs::world{};
    config_ = EngineConfig{};

    SPDLOG_INFO("Engine shutdown complete");
    spdlog::shutdown();

    started_ = false;
}

void Engine::run() {
    while (!should_close()) {
        platform_.poll();
        renderer_.render_frame();
    }
}

} // namespace engine
