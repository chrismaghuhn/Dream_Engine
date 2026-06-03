#include "engine/Engine.hpp"

#include "engine/core/CrashHandlerWin32.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/core/Log.hpp"
#include "engine/gameplay/CameraSystem.hpp"
#include "engine/render/ThinTerrainPreview.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/world/StreamingSystem.hpp"
#include "engine/world/WorldModule.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
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

    // Step 6: Flecs world + world events module
    world_ = flecs::world();
    world_.import<WorldModule>();
    CameraSystem::register_module(world_);
    player_fly_ = CameraSystem::spawn_player_fly(world_);

    // Step 7: JobSystem
    jobs_.init(config_.threads());

    // Step 8: Platform / GLFW window
    if (!platform_.init(1280, 720, "VoxelEngine")) {
        SPDLOG_ERROR("Failed to create GLFW window");
        jobs_.shutdown();
        world_ = flecs::world{};
        return false;
    }

    input_.set_cursor_captured(platform_.window(), true);

    // Step 9: Renderer (Vulkan instance/device/swapchain) -> GpuCaps
    if (!renderer_.init(platform_, config_.memory())) {
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

    chunk_store_.init(static_cast<uint32_t>(config_.streaming().max_loaded_chunks));
    thin_terrain_.init(world_, chunk_store_, config_.world());
    thin_terrain_.build_cpu_meshes();
    SPDLOG_INFO(
        "ChunkStore init: max {} chunks, streaming radius xz={} y={}",
        config_.streaming().max_loaded_chunks,
        config_.streaming().horizontal_radius_chunks,
        config_.streaming().vertical_radius_chunks);

    started_ = true;
    SPDLOG_INFO("Engine startup complete (steps 1-11)");
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
    player_fly_ = flecs::entity{};
    world_ = flecs::world{};
    config_ = EngineConfig{};

    SPDLOG_INFO("Engine shutdown complete");
    spdlog::shutdown();

    started_ = false;
}

void Engine::render_build(std::uint32_t snapshot_slot) {
    auto* camera_component = player_fly_.get_mut<CameraComponent>();
    if (!camera_component) {
        return;
    }

    const float aspect_ratio = renderer_.aspect_ratio();
    WorldRenderSnapshot& snapshot = renderer_.snapshot_ring().snapshot(snapshot_slot);
    CameraSystem::build_render_snapshot(
        *camera_component, origin_rebase_.render_origin(), aspect_ratio, snapshot, frame_index_);

    thin_terrain_.ensure_gpu_slots(renderer_.mesh_pool(), frame_index_);
    if (!thin_terrain_.uploads_queued()) {
        thin_terrain_.queue_uploads(renderer_.mesh_upload_queue());
    }
    thin_terrain_.fill_snapshot(snapshot, origin_rebase_.render_origin());
}

void Engine::run() {
    using clock = std::chrono::steady_clock;
    auto last_chunk_log = clock::now();

    while (!should_close()) {
        platform_.poll();
        input_.begin_frame(platform_.window());

        if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
            CameraSystem::update_from_input(*camera_component, input_, CameraSystem::kDefaultFlySpeed);
            origin_rebase_.maybe_rebase(
                world_, camera_component->camera.position, config_.world());

            const glm::ivec3 world_blocks = glm::ivec3(glm::floor(camera_component->camera.position));
            const WorldPosition player_pos =
                WorldPosition::from_world_blocks(world_blocks.x, world_blocks.y, world_blocks.z);
            update_streaming(
                chunk_store_, world_, config_.streaming(), config_.world(), player_pos);
        }

        const auto now = clock::now();
        if (now - last_chunk_log >= std::chrono::seconds(1)) {
            SPDLOG_INFO("Loaded chunks: {}", chunk_store_.loaded_count());
            last_chunk_log = now;
        }

        const std::uint32_t snapshot_slot = renderer_.snapshot_ring().pick_write_slot();
        render_build(snapshot_slot);
        renderer_.render_frame(snapshot_slot);
        ++frame_index_;
    }
}

} // namespace engine
