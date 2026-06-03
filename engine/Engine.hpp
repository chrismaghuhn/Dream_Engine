#pragma once

#include "engine/core/EngineConfig.hpp"
#include "engine/core/JobSystem.hpp"
#include "engine/platform/Input.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/world/OriginRebase.hpp"

#include <flecs.h>

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

private:
    void render_build(std::uint32_t snapshot_slot);

    EngineConfig config_{};
    JobSystem jobs_{};
    Platform platform_{};
    Input input_{};
    Renderer renderer_{};
    OriginRebase origin_rebase_{};
    flecs::world world_{};
    flecs::entity player_fly_{};
    std::uint64_t frame_index_ = 0;
    bool started_ = false;
};

} // namespace engine
