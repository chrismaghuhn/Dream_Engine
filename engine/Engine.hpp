#pragma once

#include "engine/core/EngineConfig.hpp"
#include "engine/core/JobSystem.hpp"
#include "engine/platform/Input.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/core/SimClock.hpp"
#include "engine/render/ThinTerrainPreview.hpp"
#include "engine/render/StreamingTerrainSystem.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/ui/UiHost.hpp"
#include "engine/world/ChunkLifecycle.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/OriginRebase.hpp"
#include "engine/gameplay/CreativeBlockPicker.hpp"
#include "engine/gameplay/Inventory.hpp"
#include "engine/gameplay/PlayerMotor.hpp"
#include "engine/gameplay/PlayerMotorConfig.hpp"
#include "engine/gameplay/PlayerSpawnReadyGate.hpp"
#include "engine/gameplay/VoxelMovementConfig.hpp"
#include "engine/physics/PhysicsSystem.hpp"
#include "engine/persist/SaveService.hpp"
#include "engine/world/WorldPosition.hpp"

#include <array>
#include <filesystem>
#include <flecs.h>
#include <string>

namespace engine {

class Engine {
public:
    Engine() = default;
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool startup();
    void shutdown();
    void run();

    [[nodiscard]] bool should_close() const { return platform_.should_close(); }
    [[nodiscard]] const EngineConfig& config() const { return config_; }
    [[nodiscard]] JobSystem& jobs() { return jobs_; }
    [[nodiscard]] flecs::world& world() { return world_; }
    [[nodiscard]] Platform& platform() { return platform_; }
    [[nodiscard]] Renderer& renderer() { return renderer_; }
    [[nodiscard]] ChunkStore& chunk_store() { return chunk_store_; }

private:
    void render_build(std::uint32_t snapshot_slot);
    [[nodiscard]] SaveWorldRequest make_save_request() const;
    [[nodiscard]] WorldPosition current_player_position() const;
    void apply_player_position(const WorldPosition& position);
    bool try_load_world_save();
    bool save_world_to_disk();
    void tick_player_simulation();
    void refresh_spawn_gate();
    [[nodiscard]] Capsule player_capsule() const;

    EngineConfig config_{};
    JobSystem jobs_{};
    Platform platform_{};
    Input input_{};
    Renderer renderer_{};
    UiHost ui_host_{};
    SimClock sim_clock_{};
    std::uint64_t sim_tick_ = 0;
    OriginRebase origin_rebase_{};
    ChunkStore chunk_store_{};
    ThinTerrainPreview thin_terrain_{};
    StreamingTerrainSystem streaming_terrain_{};
    ChunkGpuServices chunk_gpu_services_{};
    flecs::world world_{};
    flecs::entity player_fly_{};
    PhysicsSystem physics_{};
    PlayerMotor player_motor_{};
    PlayerMotorConfig player_motor_config_{};
    VoxelMovementConfig voxel_movement_config_{};
    PlayerSpawnReadyGate spawn_gate_{};
    bool walk_mode_ = true;
    bool fly_mode_toggle_down_ = false;
    CreativeBlockPicker creative_picker_{};
    Inventory inventory_{};
    bool inventory_open_ = false;
    bool inventory_toggle_down_ = false;
    std::array<bool, kHotbarSlots> hotbar_slot_down_{};
    std::filesystem::path saves_root_;
    std::string world_name_ = "default";
    std::uint64_t frame_index_ = 0;
    bool started_ = false;
};

} // namespace engine
