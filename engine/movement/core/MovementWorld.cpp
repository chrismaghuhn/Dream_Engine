#include "engine/movement/core/MovementWorld.hpp"

namespace engine::movement {

EntityId MovementWorld::spawn(const PersistentId& persistent_id) {
    const EntityId id = entities_.create();
    const std::uint32_t index = id.index();
    if (index >= persistent_by_index_.size()) {
        persistent_by_index_.resize(index + 1);
    }
    persistent_by_index_[index] = persistent_id;
    by_hash_[persistent_id.hash()] = id;
    return id;
}

void MovementWorld::destroy(EntityId id) {
    if (!entities_.is_alive(id)) {
        return;
    }
    transforms_.remove(id);
    colliders_.remove(id);
    controllers_.remove(id);
    camera_rigs_.remove(id);
    debug_names_.remove(id);

    const std::uint32_t index = id.index();
    if (index < persistent_by_index_.size()) {
        const std::uint64_t hash = persistent_by_index_[index].hash();
        auto it = by_hash_.find(hash);
        if (it != by_hash_.end() && it->second == id) {
            by_hash_.erase(it);
        }
        persistent_by_index_[index] = PersistentId{};
    }

    entities_.destroy(id);
}

EntityId MovementWorld::find(std::uint64_t persistent_hash) const {
    const auto it = by_hash_.find(persistent_hash);
    if (it == by_hash_.end()) {
        return EntityId::null();
    }
    if (!entities_.is_alive(it->second)) {
        return EntityId::null();
    }
    return it->second;
}

const PersistentId* MovementWorld::persistent_id(EntityId id) const {
    if (!entities_.is_alive(id)) {
        return nullptr;
    }
    return persistent_id_by_index(id.index());
}

const PersistentId* MovementWorld::persistent_id_by_index(std::uint32_t index) const {
    if (index < persistent_by_index_.size() && !persistent_by_index_[index].str().empty()) {
        return &persistent_by_index_[index];
    }
    return nullptr;
}

void MovementWorld::clear() {
    entities_.clear();
    transforms_.clear();
    colliders_.clear();
    controllers_.clear();
    camera_rigs_.clear();
    debug_names_.clear();
    persistent_by_index_.clear();
    by_hash_.clear();
}

} // namespace engine::movement
