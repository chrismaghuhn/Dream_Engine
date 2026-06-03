#include "engine/physics/DebrisSystem.hpp"

#include "engine/world/WorldEvents.hpp"

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace engine {

float debris_despawn_radius(const EngineConfig& config) {
    if (config.destruction().debris_despawn_radius > 0.f) {
        return config.destruction().debris_despawn_radius;
    }
    return static_cast<float>(config.streaming().horizontal_radius_chunks * 32);
}

DebrisConfig debris_config_from_engine(const EngineConfig& config) {
    return DebrisConfig{
        .max_active_debris = config.destruction().max_active_debris,
        .despawn_radius = debris_despawn_radius(config),
    };
}

void DebrisSystem::init(flecs::world& world, PhysicsSystem& physics, const DebrisConfig& config) {
    physics_ = &physics;
    max_active_ = config.max_active_debris;
    despawn_radius_ = config.despawn_radius;
    active_count_ = std::make_shared<std::atomic<int>>(0);
    debris_query_ = world.query<DebrisTag, DebrisTransform>();

    world.component<DebrisTag>();
    world.component<DebrisTransform>();
    world.component<DebrisPhysicsBody>();

    PhysicsSystem* physics_ptr = physics_;
    const int max_active = max_active_;
    std::shared_ptr<std::atomic<int>> active_count = active_count_;

    world.observer()
        .event<EvtBlockBroken>()
        .with<ChunkCoord>()
        .run([physics_ptr, max_active, active_count](flecs::iter& it) {
            while (it.next()) {
                const EvtBlockBroken* evt = it.param<EvtBlockBroken>();
                if (evt == nullptr) {
                    continue;
                }
                if (max_active <= 0) {
                    continue;
                }

                if (active_count->load(std::memory_order_relaxed) >= max_active) {
                    continue;
                }

                const glm::ivec3 world_blocks = glm::ivec3(evt->coord) * 32 + evt->block_local;
                const glm::vec3 center = glm::vec3(world_blocks) + glm::vec3(0.5f);

                it.world().defer_begin();
                flecs::entity debris =
                    it.world().entity().add<DebrisTag>().set<DebrisTransform>({center});

                if (physics_ptr != nullptr && physics_ptr->is_active()) {
                    const DebrisBodyHandle handle = physics_ptr->create_debris_box(center);
                    if (handle.valid) {
                        debris.set<DebrisPhysicsBody>({handle});
                    }
                }
                it.world().defer_end();

                active_count->fetch_add(1, std::memory_order_relaxed);
            }
        });
}

void DebrisSystem::tick(flecs::world& world, const glm::vec3& player_position) {
    if (despawn_radius_ <= 0.f || active_count_ == nullptr) {
        return;
    }

    const float radius_sq = despawn_radius_ * despawn_radius_;

    std::vector<flecs::entity> to_despawn{};
    debris_query_.each([&](flecs::entity entity, DebrisTag, DebrisTransform& transform) {
        const glm::vec3 delta = transform.position - player_position;
        if (glm::dot(delta, delta) <= radius_sq) {
            return;
        }
        to_despawn.push_back(entity);
    });

    for (flecs::entity entity : to_despawn) {
        if (physics_ != nullptr) {
            if (DebrisPhysicsBody* body = entity.get_mut<DebrisPhysicsBody>()) {
                physics_->destroy_debris_body(body->handle);
            }
        }
        entity.destruct();
        active_count_->fetch_sub(1, std::memory_order_relaxed);
    }
}

int DebrisSystem::active_count(const flecs::world& /*world*/) const {
    if (active_count_ == nullptr) {
        return 0;
    }
    return active_count_->load(std::memory_order_relaxed);
}

} // namespace engine
