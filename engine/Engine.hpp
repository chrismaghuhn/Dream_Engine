#pragma once

#include "engine/core/EngineConfig.hpp"
#include "engine/core/JobSystem.hpp"
#include "engine/platform/Platform.hpp"
#include "engine/render/Renderer.hpp"

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
    struct VoxelEngineModule {
        explicit VoxelEngineModule(flecs::world& /*ecs*/) {}
    };

    EngineConfig config_{};
    JobSystem jobs_{};
    Platform platform_{};
    Renderer renderer_{};
    flecs::world world_{};
    bool started_ = false;
};

} // namespace engine
