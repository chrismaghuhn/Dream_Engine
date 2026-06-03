#include "engine/Engine.hpp"

#include "engine/core/CrashHandlerWin32.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/core/Log.hpp"
#include "engine/gameplay/BlockInteraction.hpp"
#include "engine/gameplay/CameraSystem.hpp"
#include "engine/gameplay/PlayerMotor.hpp"
#include "engine/physics/VoxelCapsuleResolver.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/world/WorldEvents.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/StreamingSystem.hpp"
#include "engine/world/WorldModule.hpp"
#include "engine/persist/SaveService.hpp"
#include "engine/gameplay/CameraSystem.hpp"

#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <filesystem>
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

    saves_root_ = std::filesystem::path(ENGINE_SOURCE_DIR) / "saves";
    chunk_store_.init(static_cast<uint32_t>(config_.streaming().max_loaded_chunks));
    inventory_.seed_default_hotbar();
    (void)try_load_world_save();

    if (!physics_.init()) {
        SPDLOG_ERROR("Failed to initialize PhysicsSystem");
        ui_host_.shutdown();
        renderer_.shutdown();
        platform_.shutdown();
        jobs_.shutdown();
        world_ = flecs::world{};
        return false;
    }
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

    refresh_spawn_gate();
    if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
        player_motor_.sync_capsule_from_camera(camera_component->camera.position);
    }

    started_ = true;
    SPDLOG_INFO("Engine startup complete (steps 1-11)");
    return true;
}

void Engine::shutdown() {
    if (!started_) {
        return;
    }

    (void)save_world_to_disk();
    physics_.shutdown();
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

SaveWorldRequest Engine::make_save_request() {
    SaveWorldRequest request{};
    request.saves_root = saves_root_;
    request.world_name = world_name_;
    request.world_config = config_.world();
    request.player_position = current_player_position();
    request.inventory = inventory_.snapshot();
    request.jobs = &jobs_;
    return request;
}

WorldPosition Engine::current_player_position() const {
    if (const auto* camera_component = player_fly_.get<CameraComponent>()) {
        const glm::ivec3 world_blocks =
            glm::ivec3(glm::floor(camera_component->camera.position));
        return WorldPosition::from_world_blocks(world_blocks.x, world_blocks.y, world_blocks.z);
    }
    return {};
}

void Engine::apply_player_position(const WorldPosition& position) {
    if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
        camera_component->camera.position =
            glm::vec3(position.chunk) * 32.f + position.local;
    }
}

bool Engine::try_load_world_save() {
    const SaveWorldRequest request = make_save_request();
    if (!SaveService::world_save_exists(request)) {
        return false;
    }

    WorldPosition loaded_position{};
    InventorySnapshot loaded_inventory{};
    if (!SaveService::load_world(request, loaded_position, loaded_inventory, chunk_store_)) {
        SPDLOG_WARN("Failed to load world save from {}", SaveService::world_dir(request).string());
        return false;
    }

    apply_player_position(loaded_position);
    inventory_.apply_snapshot(loaded_inventory);
    spawn_gate_.reset();
    SPDLOG_INFO("Loaded world save from {}", SaveService::world_dir(request).string());
    return true;
}

bool Engine::save_world_to_disk() {
    const SaveWorldRequest request = make_save_request();
    if (!SaveService::save_world(request, chunk_store_)) {
        SPDLOG_WARN("Failed to save world to {}", SaveService::world_dir(request).string());
        return false;
    }

    SPDLOG_INFO("Saved world to {}", SaveService::world_dir(request).string());
    return true;
}

Capsule Engine::player_capsule() const {
    if (const auto* camera_component = player_fly_.get<CameraComponent>()) {
        Capsule capsule{};
        capsule.radius = PlayerMotor::kDefaultRadius;
        capsule.half_height = PlayerMotor::kDefaultHalfHeight;
        capsule.center = camera_component->camera.position - glm::vec3{0.f, PlayerMotor::kEyeHeight, 0.f};
        return capsule;
    }
    return {};
}

void Engine::refresh_spawn_gate() {
    if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
        const Capsule capsule = player_capsule();
        const glm::ivec3 world_blocks = glm::ivec3(glm::floor(camera_component->camera.position));
        const ChunkCoord spawn_chunk = block_to_chunk(world_blocks.x, world_blocks.y, world_blocks.z);
        const WorldPosition player_pos = WorldPosition::from_world_blocks(
            world_blocks.x, world_blocks.y, world_blocks.z);
        update_streaming(
            chunk_store_, world_, config_.streaming(), config_.world(), player_pos);
        (void)spawn_gate_.update(chunk_store_, capsule, spawn_chunk);
    }
}

void Engine::tick_player_simulation() {
    if (!spawn_gate_.is_ready()) {
        return;
    }

    auto* camera_component = player_fly_.get_mut<CameraComponent>();
    if (!camera_component) {
        return;
    }

    const bool was_grounded = player_motor_.state().on_ground;
    player_motor_.tick_walk(
        camera_component->camera,
        input_,
        static_cast<float>(SimClock::fixed_dt),
        chunk_store_,
        player_motor_config_,
        voxel_movement_config_);

    origin_rebase_.maybe_rebase(world_, camera_component->camera.position, config_.world());

    const bool grounded = player_motor_.state().on_ground;
    const glm::vec3 foot_pos = player_motor_.state().capsule.center;
    const flecs::entity world_root = world_.lookup("WorldRoot");
    if (!world_root.is_alive()) {
        return;
    }

    if (grounded && !was_grounded) {
        const EvtPlayerLanded evt{.world_position = foot_pos};
        world_.event<EvtPlayerLanded>().id<WorldRoot>().entity(world_root).ctx(evt).emit();
    }

    const glm::vec3 horiz_vel{
        player_motor_.state().velocity.x, 0.f, player_motor_.state().velocity.z};
    if (grounded && glm::length(horiz_vel) > 0.1f) {
        const EvtPlayerFootstep evt{.world_position = foot_pos};
        world_.event<EvtPlayerFootstep>().id<WorldRoot>().entity(world_root).ctx(evt).emit();
    }
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
        sim_tick_ += sim_clock_.step([this]() { tick_player_simulation(); });

        if (input_.save_pressed()) {
            (void)save_world_to_disk();
        }
        if (input_.load_pressed()) {
            (void)try_load_world_save();
        }

        if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
            const bool fly_toggle_now = glfwGetKey(platform_.window(), GLFW_KEY_F) == GLFW_PRESS;
            if (fly_toggle_now && !fly_mode_toggle_down_) {
                walk_mode_ = !walk_mode_;
            }
            fly_mode_toggle_down_ = fly_toggle_now;

            const bool inventory_toggle_now =
                glfwGetKey(platform_.window(), GLFW_KEY_I) == GLFW_PRESS;
            if (inventory_toggle_now && !inventory_toggle_down_) {
                inventory_open_ = !inventory_open_;
                input_.set_cursor_captured(platform_.window(), !inventory_open_);
            }
            inventory_toggle_down_ = inventory_toggle_now;

            for (int slot = 0; slot < static_cast<int>(kHotbarSlots); ++slot) {
                const int key = GLFW_KEY_1 + slot;
                const bool slot_now = glfwGetKey(platform_.window(), key) == GLFW_PRESS;
                if (slot_now && !hotbar_slot_down_[static_cast<size_t>(slot)]) {
                    inventory_.set_hotbar_selected(static_cast<std::uint8_t>(slot));
                }
                hotbar_slot_down_[static_cast<size_t>(slot)] = slot_now;
            }

            if (!spawn_gate_.is_ready()) {
                refresh_spawn_gate();
            }

            const bool look_from_input = walk_mode_ && spawn_gate_.is_ready() && !inventory_open_;
            if (look_from_input) {
                CameraSystem::update_look_from_input(*camera_component, input_);
            } else if (!inventory_open_) {
                CameraSystem::update_from_input(*camera_component, input_, CameraSystem::kDefaultFlySpeed);
                origin_rebase_.maybe_rebase(
                    world_, camera_component->camera.position, config_.world());
            }

            const CreativeBlockPicker* creative_picker =
                config_.creative_place() ? &creative_picker_ : nullptr;
            handle_block_input(
                world_,
                chunk_store_,
                camera_component->camera,
                input_,
                config_.world(),
                inventory_,
                config_.creative_place(),
                creative_picker,
                sim_tick_,
                player_fly_.id());

            const glm::ivec3 world_blocks = glm::ivec3(glm::floor(camera_component->camera.position));
            const WorldPosition player_pos =
                WorldPosition::from_world_blocks(world_blocks.x, world_blocks.y, world_blocks.z);
            update_streaming(
                chunk_store_, world_, config_.streaming(), config_.world(), player_pos);

            if (!walk_mode_ || !spawn_gate_.is_ready()) {
                origin_rebase_.maybe_rebase(
                    world_, camera_component->camera.position, config_.world());
            }
        }

        const auto now = clock::now();
        if (now - last_chunk_log >= std::chrono::seconds(1)) {
            SPDLOG_INFO("Loaded chunks: {}", chunk_store_.loaded_count());
            last_chunk_log = now;
        }

        const std::uint32_t snapshot_slot = renderer_.snapshot_ring().pick_write_slot();
        render_build(snapshot_slot);

        UiInventoryState inventory_ui{
            .inventory = &inventory_,
            .inventory_open = inventory_open_,
        };
        ui_host_.new_frame(
            UiOverlayStats{
                .fps = fps,
                .sim_tick = sim_tick_,
                .loaded_chunks = chunk_store_.loaded_count(),
            },
            inventory_ui);
        inventory_open_ = inventory_ui.inventory_open;
        renderer_.render_frame(snapshot_slot);
        ++frame_index_;
    }
}

} // namespace engine
