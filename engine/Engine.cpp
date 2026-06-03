#include "engine/Engine.hpp"

#include "engine/core/CrashHandlerWin32.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/core/Log.hpp"
#include "engine/gameplay/CameraSystem.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/world/ChunkLifecycle.hpp"
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

    crash_handler_install();
    log_init();

    const CpuHardware cpu = HardwareProbe::run_cpu();
    SPDLOG_INFO("CPU probe: {} physical / {} logical cores, {} MiB RAM",
                cpu.physical_cores,
                cpu.logical_cores,
                cpu.ram_bytes / (1024 * 1024));

    const std::string config_path = std::string(ENGINE_SOURCE_DIR) + "/assets/default.toml";
    config_.load_toml(config_path);
    config_.finalize_cpu(cpu);
    SPDLOG_INFO("EngineConfig loaded from {}", config_path);

    world_ = flecs::world();
    world_.import<WorldModule>();
    CameraSystem::register_module(world_);
    player_fly_ = CameraSystem::spawn_player_fly(world_);

    jobs_.init(config_.threads());

    if (!platform_.init(1280, 720, "VoxelEngine")) {
        SPDLOG_ERROR("Failed to create GLFW window");
        jobs_.shutdown();
        world_ = flecs::world{};
        return false;
    }

    input_.set_cursor_captured(platform_.window(), true);

    if (!renderer_.init(platform_, config_.memory())) {
        SPDLOG_ERROR("Failed to initialize Renderer");
        platform_.shutdown();
        jobs_.shutdown();
        world_ = flecs::world{};
        return false;
    }

    config_.finalize_gpu(renderer_.gpu_caps());
    SPDLOG_INFO("GPU finalize: {} MiB mesh VRAM budget, preset {}",
                config_.memory().gpu_mesh_vram / (1024 * 1024),
                static_cast<int>(config_.render_preset()));

    if (!ui_host_.init(platform_, renderer_)) {
        SPDLOG_ERROR("Failed to initialize UiHost");
        renderer_.shutdown();
        platform_.shutdown();
        jobs_.shutdown();
        world_ = flecs::world{};
        return false;
    }
    renderer_.set_ui_host(&ui_host_);

    chunk_gpu_services_.deferred_free = &renderer_.deferred_free_queue();
    chunk_gpu_services_.frame_index = [this]() { return frame_index_; };
    set_chunk_gpu_services(&chunk_gpu_services_);

    chunk_store_.init(static_cast<uint32_t>(config_.streaming().max_loaded_chunks));
    if (config_.thin_terrain_preview()) {
        thin_terrain_.init(world_, chunk_store_, config_.world());
        thin_terrain_.build_cpu_meshes();
        SPDLOG_INFO("Terrain render mode: thin preview (single chunk at origin)");
    } else {
        streaming_terrain_.init(world_, chunk_store_, jobs_, config_.world());
        streaming_terrain_.register_observers(world_);
        SPDLOG_INFO("Terrain render mode: streaming multi-chunk");
    }
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

    set_chunk_gpu_services(nullptr);
    ui_host_.shutdown();
    renderer_.shutdown();
    platform_.shutdown();
    jobs_.shutdown();
    player_fly_ = flecs::entity{};
    world_ = flecs::world{};
    config_ = EngineConfig{};
    sim_clock_.reset();
    sim_tick_ = 0;

    SPDLOG_INFO("Engine shutdown complete");
    spdlog::shutdown();

    started_ = false;
}

void Engine::render_build(std::uint32_t snapshot_slot) {
    auto* camera_component = player_fly_.get_mut<CameraComponent>();
    if (!camera_component) {
        return;
    }

    jobs_.wait_meshing();

    const float aspect_ratio = renderer_.aspect_ratio();
    WorldRenderSnapshot& snapshot = renderer_.snapshot_ring().snapshot(snapshot_slot);
    CameraSystem::build_render_snapshot(
        *camera_component, origin_rebase_.render_origin(), aspect_ratio, snapshot, frame_index_);

    if (config_.thin_terrain_preview()) {
        thin_terrain_.ensure_gpu_slots(renderer_.mesh_pool(), frame_index_);
        if (!thin_terrain_.uploads_queued()) {
            thin_terrain_.queue_uploads(renderer_.mesh_upload_queue());
        }
        thin_terrain_.fill_snapshot(snapshot, origin_rebase_.render_origin());
        return;
    }

    streaming_terrain_.on_frame(
        frame_index_, renderer_.mesh_pool(), renderer_.mesh_upload_queue(), renderer_.deferred_free_queue());
    streaming_terrain_.build_snapshot(snapshot, origin_rebase_.render_origin(), chunk_store_);
}

void Engine::run() {
    using clock = std::chrono::steady_clock;
    auto last_frame_time = clock::now();
    auto last_chunk_log = last_frame_time;
    float fps = 0.f;

    while (!should_close()) {
        platform_.poll();
        input_.begin_frame(platform_.window());

        const auto frame_start = clock::now();
        const double frame_delta =
            std::chrono::duration<double>(frame_start - last_frame_time).count();
        last_frame_time = frame_start;
        if (frame_delta > 0.0) {
            fps = static_cast<float>(1.0 / frame_delta);
        }

        sim_clock_.advance(frame_delta);
        sim_tick_ += sim_clock_.step([]() {});

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

        ui_host_.new_frame(UiOverlayStats{
            .fps = fps,
            .sim_tick = sim_tick_,
            .loaded_chunks = chunk_store_.loaded_count(),
        });
        renderer_.render_frame(snapshot_slot);
        ++frame_index_;
    }
}

} // namespace engine
