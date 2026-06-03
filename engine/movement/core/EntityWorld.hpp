#pragma once

#include <cstdint>
#include <vector>

namespace engine::movement {

// Packed handle: low 22 bits = index, high 10 bits = generation.
// Generation lets stale handles be detected after an index slot is recycled.
class EntityId {
public:
    static constexpr std::uint32_t kIndexBits = 22;
    static constexpr std::uint32_t kGenerationBits = 10;
    static constexpr std::uint32_t kIndexMask = (1u << kIndexBits) - 1u;
    static constexpr std::uint32_t kGenerationMask = (1u << kGenerationBits) - 1u;
    static constexpr std::uint32_t kMaxIndex = kIndexMask;
    static constexpr std::uint32_t kMaxGeneration = kGenerationMask;

    constexpr EntityId() = default;

    constexpr EntityId(std::uint32_t index, std::uint32_t generation)
        : value_((index & kIndexMask) | ((generation & kGenerationMask) << kIndexBits)) {}

    [[nodiscard]] constexpr std::uint32_t index() const { return value_ & kIndexMask; }
    [[nodiscard]] constexpr std::uint32_t generation() const {
        return (value_ >> kIndexBits) & kGenerationMask;
    }
    [[nodiscard]] constexpr std::uint32_t raw() const { return value_; }
    [[nodiscard]] constexpr bool is_null() const { return value_ == kNullValue; }

    friend constexpr bool operator==(EntityId lhs, EntityId rhs) {
        return lhs.value_ == rhs.value_;
    }
    friend constexpr bool operator!=(EntityId lhs, EntityId rhs) {
        return lhs.value_ != rhs.value_;
    }

    [[nodiscard]] static constexpr EntityId null() { return EntityId{}; }

private:
    static constexpr std::uint32_t kNullValue = 0xFFFFFFFFu;
    std::uint32_t value_ = kNullValue;
};

// Minimal entity registry: dense generation table + free list of recycled indices.
// Holds no component data itself; component stores key off EntityId::index().
class EntityWorld {
public:
    [[nodiscard]] EntityId create();
    void destroy(EntityId id);
    [[nodiscard]] bool is_alive(EntityId id) const;

    [[nodiscard]] std::size_t alive_count() const { return alive_count_; }
    [[nodiscard]] std::size_t capacity() const { return generations_.size(); }

    void clear();

private:
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint32_t> free_indices_;
    std::size_t alive_count_ = 0;
};

} // namespace engine::movement
