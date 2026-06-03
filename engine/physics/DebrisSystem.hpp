#pragma once

#include "engine/core/EngineConfig.hpp"
#include "engine/physics/PhysicsSystem.hpp"

#include <flecs.h>
#include <glm/glm.hpp>

#include <atomic>
#include <memory>

namespace engine {

struct DebrisTag {};

struct DebrisTransform {
    glm::vec3 position{};
};

struct DebrisPhysicsBody {
    DebrisBodyHandle handle{};
};

struct DebrisConfig {
    int max_active_debris = 0;
    float despawn_radius = 0.f;
};

[[nodiscard]] DebrisConfig debris_config_from_engine(const EngineConfig& config);

class DebrisSystem {
public:
    void init(flecs::world& world, PhysicsSystem& physics, const DebrisConfig& config);
    void tick(flecs::world& world, const glm::vec3& player_position);

    [[nodiscard]] int active_count(const flecs::world& world) const;

private:
    PhysicsSystem* physics_ = nullptr;
    int max_active_ = 0;
    float despawn_radius_ = 0.f;
    std::shared_ptr<std::atomic<int>> active_count_{};
};

} // namespace engine
