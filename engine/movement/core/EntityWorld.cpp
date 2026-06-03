#include "engine/movement/core/EntityWorld.hpp"

#include <cassert>

namespace engine::movement {

EntityId EntityWorld::create() {
    std::uint32_t index = 0;
    if (!free_indices_.empty()) {
        index = free_indices_.back();
        free_indices_.pop_back();
    } else {
        assert(generations_.size() <= EntityId::kMaxIndex && "EntityWorld index space exhausted");
        index = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(0);
    }

    ++alive_count_;
    return EntityId{index, generations_[index]};
}

void EntityWorld::destroy(EntityId id) {
    if (!is_alive(id)) {
        return;
    }

    const std::uint32_t index = id.index();
    // Bump generation so any lingering handle to this slot is invalidated. Wrap
    // around within the generation bit width so the packed value stays valid.
    generations_[index] = (generations_[index] + 1u) & EntityId::kGenerationMask;
    free_indices_.push_back(index);
    --alive_count_;
}

bool EntityWorld::is_alive(EntityId id) const {
    if (id.is_null()) {
        return false;
    }
    const std::uint32_t index = id.index();
    if (index >= generations_.size()) {
        return false;
    }
    return generations_[index] == id.generation();
}

void EntityWorld::clear() {
    generations_.clear();
    free_indices_.clear();
    alive_count_ = 0;
}

} // namespace engine::movement
