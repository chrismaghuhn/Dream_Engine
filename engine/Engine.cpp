#include "engine/Engine.hpp"

#include "engine/audio/AudioEngine.hpp"
#include "engine/core/CrashHandlerWin32.hpp"
#include "engine/core/HardwareProbe.hpp"
#include "engine/core/Log.hpp"
#include "engine/gameplay/BlockInteraction.hpp"
#include "engine/gameplay/CameraSystem.hpp"
#include "engine/gameplay/PlayerMotor.hpp"
#include "engine/physics/DebrisSystem.hpp"
#include "engine/physics/VoxelCapsuleResolver.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/procgen/TerrainGraph.hpp"
#include "engine/world/WorldEvents.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/StreamingSystem.hpp"
#include "engine/world/WorldModule.hpp"
#include "engine/persist/SaveService.hpp"
#include "engine/gameplay/CameraSystem.hpp"

#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
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
    renderer_.apply_memory_budget(config_.memory());
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
    chunk_gpu_services_.submit_snapshot_slot = [this]() { return last_submit_snapshot_slot_; };
    set_chunk_gpu_services(&chunk_gpu_services_);

    saves_root_ = std::filesystem::path(ENGINE_SOURCE_DIR) / "saves";
    chunk_store_.init(static_cast<uint32_t>(config_.streaming().max_loaded_chunks));
    inventory_.seed_default_hotbar();
    const bool loaded_save = try_load_world_save();
    if (!loaded_save) {
        TerrainGraph terrain(config_.world().world_seed, config_.world().sea_level);
        const float spawn_y = static_cast<float>(terrain.surface_height_at(0, 0)) + 2.f;
        if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
            camera_component->camera.position = glm::vec3{0.f, spawn_y, 0.f};
            SPDLOG_INFO("Player spawn at surface y={:.1f}", spawn_y);
        }
    }

    if (!physics_.init()) {
        SPDLOG_ERROR("Failed to initialize PhysicsSystem");
        ui_host_.shutdown();
        renderer_.shutdown();
        platform_.shutdown();
        jobs_.shutdown();
        world_ = flecs::world{};
        return false;
    }

    debris_.init(world_, physics_, debris_config_from_engine(config_));

    if (!audio_.init(world_, chunk_store_, config_)) {
        SPDLOG_ERROR("Failed to initialize AudioEngine");
        physics_.shutdown();
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
        SPDLOG_INFO("Terrain render mode: streaming multi-chunk");
    }
    SPDLOG_INFO(
        "ChunkStore init: max {} chunks, streaming radius xz={} y={}",
        config_.streaming().max_loaded_chunks,
        config_.streaming().horizontal_radius_chunks,
        config_.streaming().vertical_radius_chunks);

    // Load spawn chunks before observers — on_chunk_loaded invalidates neighbors and
    // would cascade hundreds of remeshes if fired during initial neighborhood load.
    refresh_spawn_gate();
    if (!config_.thin_terrain_preview()) {
        streaming_terrain_.register_observers(world_);
        if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
            streaming_terrain_.bootstrap_existing_chunks(
                chunk_store_, camera_component->camera.position);
            streaming_terrain_.warmup_meshes_near_focus(
                jobs_, camera_component->camera.position, 60);
            SPDLOG_INFO(
                "Terrain mesh warmup: {} mesh-ready sections",
                streaming_terrain_.count_mesh_ready_sections());
        }
    }
    if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
        player_motor_.sync_capsule_from_camera(camera_component->camera.position);
    }

    started_ = true;
    SPDLOG_INFO(
        "Engine startup complete (build 2026-06-03i); indirect draw buffer capacity {}",
        engine::Renderer::kIndirectBufferDraws);
    if (auto logger = spdlog::default_logger()) {
        logger->flush();
    }
    return true;
}

void Engine::shutdown() {
    if (!started_) {
        return;
    }

    (void)save_world_to_disk();
    audio_.shutdown();
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
        glm::vec3 world_pos = glm::vec3(position.chunk) * 32.f + position.local;
        TerrainGraph terrain(config_.world().world_seed, config_.world().sea_level);
        const int wx = static_cast<int>(std::floor(world_pos.x));
        const int wz = static_cast<int>(std::floor(world_pos.z));
        const float surface_y = static_cast<float>(terrain.surface_height_at(wx, wz)) + 2.f;
        if (world_pos.y < surface_y) {
            world_pos.y = surface_y;
        }
        camera_component->camera.position = world_pos;
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
        load_spawn_neighborhood(chunk_store_, world_, config_.world(), spawn_chunk);
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

    streaming_terrain_.on_frame(camera_component->camera.position,
                                last_submit_snapshot_slot_,
                                renderer_.mesh_pool(),
                                renderer_.mesh_upload_queue(),
                                renderer_.deferred_free_queue());
}

void Engine::run() {
    using clock = std::chrono::steady_clock;
    auto last_frame_time = clock::now();
    auto last_chunk_log = last_frame_time;
    float fps = 0.f;

    SPDLOG_INFO("Entering main loop");
    if (auto logger = spdlog::default_logger()) {
        logger->flush();
    }

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

        if (auto* camera_component = player_fly_.get<CameraComponent>()) {
            debris_.tick(world_, camera_component->camera.position);
        }

        audio_.tick();

        if (input_.save_pressed()) {
            (void)save_world_to_disk();
        }
        if (input_.load_pressed()) {
            (void)try_load_world_save();
        }

        if (auto* camera_component = player_fly_.get_mut<CameraComponent>()) {
            audio_.update_listener(
                camera_component->camera.position,
                camera_component->camera.forward(),
                camera_component->camera.up());
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
            // No meshing barrier here: mesh workers are self-contained (they only
            // touch their copied section snapshot, never ChunkStore), so streaming
            // may mutate the store while meshes finish asynchronously. Completions
            // are drained and validated on the main thread in on_frame().

            std::vector<ChunkCoord> chunks_loaded_this_frame;
            world_.defer_begin();
            const int chunks_loaded_count = update_streaming(
                chunk_store_,
                world_,
                config_.streaming(),
                config_.world(),
                player_pos,
                &chunks_loaded_this_frame);
            world_.defer_end();

            if (!config_.thin_terrain_preview() && chunks_loaded_count > 0) {
                // Reschedules border-healed remeshes (dispatch only, no wait); the
                // resulting meshes are picked up asynchronously by on_frame().
                streaming_terrain_.heal_seams_after_chunk_loads(chunks_loaded_this_frame);
            }

            if (!walk_mode_ || !spawn_gate_.is_ready()) {
                origin_rebase_.maybe_rebase(
                    world_, camera_component->camera.position, config_.world());
            }
        }

        const auto now = clock::now();
        if (frame_index_ < 3) {
            SPDLOG_INFO(
                "Frame {}: chunks={} draw_sections={}",
                frame_index_,
                chunk_store_.loaded_count(),
                last_draw_sections_);
            if (auto logger = spdlog::default_logger()) {
                logger->flush();
            }
        }

        if (now - last_chunk_log >= std::chrono::seconds(1)) {
            SPDLOG_INFO("Loaded chunks: {}", chunk_store_.loaded_count());
            last_chunk_log = now;
        }

        if (renderer_.device_lost()) {
            renderer_.recover_if_device_lost();
            if (!config_.thin_terrain_preview()) {
                streaming_terrain_.reset_gpu_after_device_lost();
            }
            continue;
        }

        const std::uint32_t snapshot_slot = renderer_.snapshot_ring().pick_write_slot();
        last_submit_snapshot_slot_ = snapshot_slot;
        if (renderer_.snapshot_ring().consume_pick_device_lost()) {
            renderer_.note_device_lost();
            renderer_.recover_if_device_lost();
            if (!config_.thin_terrain_preview()) {
                streaming_terrain_.reset_gpu_after_device_lost();
            }
            continue;
        }
        if (!config_.thin_terrain_preview()) {
            const std::vector<MeshUploadFlushMark> gpu_ready_marks =
                renderer_.consume_upload_marks_for_snapshot(snapshot_slot);
            if (!gpu_ready_marks.empty()) {
                streaming_terrain_.mark_uploads_complete(gpu_ready_marks, renderer_.mesh_pool());
            }
        }
        render_build(snapshot_slot);

        const std::uint32_t draw_sections = config_.thin_terrain_preview()
            ? static_cast<std::uint32_t>(
                  renderer_.snapshot_ring().snapshot(snapshot_slot).opaque_sections.size() +
                  renderer_.snapshot_ring().snapshot(snapshot_slot).water_sections.size())
            : last_draw_sections_;
        const std::uint32_t mesh_ready_sections = config_.thin_terrain_preview()
            ? 0
            : static_cast<std::uint32_t>(streaming_terrain_.count_mesh_ready_sections());
        const std::uint32_t gpu_ready_sections = config_.thin_terrain_preview()
            ? 0
            : static_cast<std::uint32_t>(streaming_terrain_.count_gpu_ready_sections());
        const std::uint32_t pending_mesh_jobs = config_.thin_terrain_preview()
            ? 0
            : static_cast<std::uint32_t>(streaming_terrain_.count_pending_mesh_jobs());
        const std::uint32_t lod1_draw_chunks = config_.thin_terrain_preview()
            ? 0
            : streaming_terrain_.count_lod1_draw_chunks();
        const std::uint32_t pending_lod1_mesh_jobs = config_.thin_terrain_preview()
            ? 0
            : static_cast<std::uint32_t>(streaming_terrain_.count_pending_lod1_mesh_jobs());
        const std::uint32_t water_border_lod0_forced = config_.thin_terrain_preview()
            ? 0
            : streaming_terrain_.count_water_border_lod0_forced();
        const std::uint32_t gpu_mesh_budget_mib =
            static_cast<std::uint32_t>(renderer_.mesh_pool().bytes_budget() / (1024 * 1024));

        UiInventoryState inventory_ui{
            .inventory = &inventory_,
            .inventory_open = inventory_open_,
        };
        ui_host_.new_frame(
            UiOverlayStats{
                .fps = fps,
                .sim_tick = sim_tick_,
                .loaded_chunks = chunk_store_.loaded_count(),
                .draw_sections = draw_sections,
                .mesh_ready_sections = mesh_ready_sections,
                .empty_skip_sections = config_.thin_terrain_preview()
                    ? 0
                    : static_cast<std::uint32_t>(streaming_terrain_.count_empty_skip_sections()),
                .occluded_skip_sections = config_.thin_terrain_preview()
                    ? 0
                    : static_cast<std::uint32_t>(streaming_terrain_.count_occluded_skip_sections()),
                .gpu_ready_sections = gpu_ready_sections,
                .pending_mesh_jobs = pending_mesh_jobs,
                .lod1_draw_chunks = lod1_draw_chunks,
                .pending_lod1_mesh_jobs = pending_lod1_mesh_jobs,
                .water_border_lod0_forced = water_border_lod0_forced,
                .gpu_mesh_budget_mib = gpu_mesh_budget_mib,
            },
            inventory_ui);
        inventory_open_ = inventory_ui.inventory_open;

        if (config_.thin_terrain_preview()) {
            renderer_.render_frame(snapshot_slot);
        } else {
            renderer_.render_frame(snapshot_slot, [&](WorldRenderSnapshot& snap) {
                streaming_terrain_.build_snapshot(
                    snap, origin_rebase_.render_origin(), chunk_store_, renderer_.mesh_pool());
                last_draw_sections_ = static_cast<std::uint32_t>(snap.opaque_sections.size() +
                                                                 snap.water_sections.size());
            });
        }
        ++frame_index_;
    }
}

} // namespace engine
