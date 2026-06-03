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
#include "engine/persist/SaveService.hpp"
#include "engine/world/WorldPosition.hpp"

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
    CreativeBlockPicker creative_picker_{};
    std::filesystem::path saves_root_;
    std::string world_name_ = "default";
    std::uint64_t frame_index_ = 0;
    bool started_ = false;
};

} // namespace engine
