#pragma once

#include "engine/movement/core/EntityWorld.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace engine::movement {

// Sparse component storage keyed by EntityId::index(). Supports the lifecycle
// operations the spec requires for every store: add, get (mutable/const), has,
// remove, and destroy-on-entity-removal (driven by MovementWorld).
//
// Not a full ECS: no archetypes, no queries, no reflection. Just a typed map
// from entity index to component value, plus a packed iteration helper.
template <class T>
class ComponentStore {
public:
    T& add(EntityId id, T value = T{}) {
        const std::uint32_t index = id.index();
        ensure(index);
        if (!present_[index]) {
            present_[index] = 1u;
            ++count_;
        }
        data_[index] = std::move(value);
        return data_[index];
    }

    [[nodiscard]] bool has(EntityId id) const {
        const std::uint32_t index = id.index();
        return index < present_.size() && present_[index] != 0u;
    }

    [[nodiscard]] T* get(EntityId id) {
        const std::uint32_t index = id.index();
        if (index < present_.size() && present_[index] != 0u) {
            return &data_[index];
        }
        return nullptr;
    }

    [[nodiscard]] const T* get(EntityId id) const {
        const std::uint32_t index = id.index();
        if (index < present_.size() && present_[index] != 0u) {
            return &data_[index];
        }
        return nullptr;
    }

    void remove(EntityId id) {
        const std::uint32_t index = id.index();
        if (index < present_.size() && present_[index] != 0u) {
            present_[index] = 0u;
            data_[index] = T{};
            --count_;
        }
    }

    [[nodiscard]] std::size_t size() const { return count_; }

    void clear() {
        data_.clear();
        present_.clear();
        count_ = 0;
    }

    // Visit every (index, component) pair currently present. The functor takes
    // (std::uint32_t index, T& component).
    template <class Fn>
    void for_each(Fn&& fn) {
        for (std::uint32_t i = 0; i < present_.size(); ++i) {
            if (present_[i] != 0u) {
                fn(i, data_[i]);
            }
        }
    }

    template <class Fn>
    void for_each(Fn&& fn) const {
        for (std::uint32_t i = 0; i < present_.size(); ++i) {
            if (present_[i] != 0u) {
                fn(i, data_[i]);
            }
        }
    }

private:
    void ensure(std::uint32_t index) {
        if (index >= data_.size()) {
            data_.resize(index + 1);
            present_.resize(index + 1, 0u);
        }
    }

    std::vector<T> data_;
    std::vector<std::uint8_t> present_;
    std::size_t count_ = 0;
};

} // namespace engine::movement
