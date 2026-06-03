#pragma once

#include "engine/gameplay/BlockRegistry.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace engine {

using ItemId = std::uint16_t;

constexpr ItemId ITEM_EMPTY = 0;

constexpr std::size_t kHotbarSlots = 9;
constexpr std::size_t kInventoryColumns = 9;
constexpr std::size_t kInventoryRows = 4;
constexpr std::size_t kMainGridSlots = kInventoryColumns * kInventoryRows;
constexpr std::size_t kInventoryTotalSlots = kHotbarSlots + kMainGridSlots;

struct ItemStack {
    ItemId item_id = ITEM_EMPTY;
    std::uint16_t count = 0;

    [[nodiscard]] bool empty() const { return count == 0 || item_id == ITEM_EMPTY; }

    void clear() {
        item_id = ITEM_EMPTY;
        count = 0;
    }
};

struct InventorySnapshot {
    std::array<ItemStack, kHotbarSlots> hotbar{};
    std::uint8_t hotbar_selected = 0;
};

struct Inventory {
    std::array<ItemStack, kHotbarSlots> hotbar{};
    std::array<ItemStack, kMainGridSlots> main{};
    std::uint8_t hotbar_selected = 0;

    [[nodiscard]] ItemStack& slot(std::size_t index) {
        if (index < kHotbarSlots) {
            return hotbar[index];
        }
        return main[index - kHotbarSlots];
    }

    [[nodiscard]] const ItemStack& slot(std::size_t index) const {
        if (index < kHotbarSlots) {
            return hotbar[index];
        }
        return main[index - kHotbarSlots];
    }

    [[nodiscard]] bool is_hotbar_slot(std::size_t index) const { return index < kHotbarSlots; }

    [[nodiscard]] BlockId selected_block_id() const {
        if (hotbar_selected >= kHotbarSlots) {
            return BLOCK_AIR;
        }
        const ItemStack& stack = hotbar[hotbar_selected];
        if (stack.empty()) {
            return BLOCK_AIR;
        }
        return static_cast<BlockId>(stack.item_id);
    }

    void set_hotbar_selected(std::uint8_t index) {
        hotbar_selected = static_cast<std::uint8_t>(std::min<std::size_t>(index, kHotbarSlots - 1));
    }

    void seed_default_hotbar() {
        hotbar[0] = ItemStack{static_cast<ItemId>(BLOCK_STONE), 64};
        hotbar[1] = ItemStack{static_cast<ItemId>(BLOCK_DIRT), 64};
        hotbar[2] = ItemStack{static_cast<ItemId>(BLOCK_TORCH), 64};
        hotbar_selected = 0;
    }

    void swap_slots(std::size_t a, std::size_t b) {
        if (a == b || a >= kInventoryTotalSlots || b >= kInventoryTotalSlots) {
            return;
        }
        std::swap(slot(a), slot(b));
    }

    [[nodiscard]] InventorySnapshot snapshot() const {
        InventorySnapshot snap{};
        snap.hotbar = hotbar;
        snap.hotbar_selected = hotbar_selected;
        return snap;
    }

    void apply_snapshot(const InventorySnapshot& snap) {
        hotbar = snap.hotbar;
        hotbar_selected = snap.hotbar_selected;
        if (hotbar_selected >= kHotbarSlots) {
            hotbar_selected = 0;
        }
    }
};

[[nodiscard]] inline const char* item_display_name(ItemId item_id) {
    switch (static_cast<BlockId>(item_id)) {
    case BLOCK_STONE:
        return "Stone";
    case BLOCK_DIRT:
        return "Dirt";
    case BLOCK_GRASS:
        return "Grass";
    case BLOCK_TORCH:
        return "Torch";
    default:
        return "Empty";
    }
}

} // namespace engine
