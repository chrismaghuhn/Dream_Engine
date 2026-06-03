#pragma once

#include "engine/movement/core/ComponentStore.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/EntityWorld.hpp"
#include "engine/movement/core/PersistentId.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::movement {

using TransformStore = ComponentStore<Transform>;
using ColliderStore = ComponentStore<Collider>;
using PlayerControllerStore = ComponentStore<PlayerController>;
using CameraRigStore = ComponentStore<CameraRig>;
using DebugNameStore = ComponentStore<DebugName>;

// Owns the entity registry and every movement-v1 component store, plus the
// PersistentId <-> EntityId mapping. Destroying an entity here cascades the
// removal across all stores, satisfying the spec's component-lifecycle rule.
class MovementWorld {
public:
    [[nodiscard]] EntityWorld& entities() { return entities_; }
    [[nodiscard]] const EntityWorld& entities() const { return entities_; }

    [[nodiscard]] TransformStore& transforms() { return transforms_; }
    [[nodiscard]] const TransformStore& transforms() const { return transforms_; }
    [[nodiscard]] ColliderStore& colliders() { return colliders_; }
    [[nodiscard]] const ColliderStore& colliders() const { return colliders_; }
    [[nodiscard]] PlayerControllerStore& controllers() { return controllers_; }
    [[nodiscard]] const PlayerControllerStore& controllers() const { return controllers_; }
    [[nodiscard]] CameraRigStore& camera_rigs() { return camera_rigs_; }
    [[nodiscard]] const CameraRigStore& camera_rigs() const { return camera_rigs_; }
    [[nodiscard]] DebugNameStore& debug_names() { return debug_names_; }
    [[nodiscard]] const DebugNameStore& debug_names() const { return debug_names_; }

    // Create an entity and register its PersistentId.
    EntityId spawn(const PersistentId& persistent_id);

    // Remove an entity and all of its components.
    void destroy(EntityId id);

    [[nodiscard]] bool is_alive(EntityId id) const { return entities_.is_alive(id); }

    // Resolve a PersistentId hash back to the live entity, or null if unknown.
    [[nodiscard]] EntityId find(std::uint64_t persistent_hash) const;
    [[nodiscard]] EntityId find(const PersistentId& id) const { return find(id.hash()); }

    // PersistentId of a live entity (empty string if none).
    [[nodiscard]] const PersistentId* persistent_id(EntityId id) const;

    // PersistentId by raw store index, ignoring generation. Used when iterating
    // component stores (which are keyed by index).
    [[nodiscard]] const PersistentId* persistent_id_by_index(std::uint32_t index) const;

    void clear();

private:
    EntityWorld entities_;
    TransformStore transforms_;
    ColliderStore colliders_;
    PlayerControllerStore controllers_;
    CameraRigStore camera_rigs_;
    DebugNameStore debug_names_;

    // index -> PersistentId for live entities.
    std::vector<PersistentId> persistent_by_index_;
    std::unordered_map<std::uint64_t, EntityId> by_hash_;
};

} // namespace engine::movement
