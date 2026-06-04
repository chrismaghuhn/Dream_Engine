# Survival Light Resource Loop - Design Spec

**Date:** 2026-06-04
**Status:** Ready for user review

## Problem Statement

The engine already supports voxel block interaction, a hotbar inventory, player
saves, and world persistence. The current gameplay loop is still mostly
creative-mode: the player can break and place blocks, but resource ownership is
not enforced as a survival rule. The next slice should turn block interaction
into a small, testable survival loop without adding crafting, tools, health,
hunger balancing, or complex drop tables.

## Goal

Create a minimal real loop:

1. Breaking a collectible solid block removes it from the world and adds one
   matching item to the player's inventory.
2. Placing a block consumes one item from the selected hotbar stack only after
   the placement mutation succeeds.
3. Saving and loading preserves the player's hotbar counts through the existing
   `player.dat` inventory snapshot.

This should make the player feel that blocks are owned resources rather than
free creative materials.

## Scope

In scope:

- Stone, Dirt, and Grass drop themselves 1:1.
- Air and Water drop nothing.
- Torch placement remains supported if the player owns Torch items.
- Placement uses the selected hotbar stack when that item maps to a placeable
  block type.
- If the selected stack is empty, placement is rejected.
- If inventory has no room for a drop, breaking is rejected and the block stays
  in the world.
- Creative or developer inventory seeding, if still needed, must be explicit
  and separate from the normal survival-light game path.
- Existing hotbar UI continues to show item counts.

Out of scope:

- Crafting.
- Tools, tool tiers, durability, or mining speed.
- Rare drops or loot tables.
- Stack splitting UI.
- Health, hunger, death, objectives, or quests.
- New block types.

## Architecture

### Inventory

`engine/gameplay/Inventory.hpp` gains focused resource operations:

- `can_add_item(ItemId item_id, uint16_t count) const`
- `bool add_item(ItemId item_id, uint16_t count)`
- `can_consume_selected(uint16_t count) const`
- `bool consume_selected(uint16_t count)`

`add_item` fills existing compatible stacks first, then empty slots. Stack size
should be capped consistently, using a named constant such as
`kMaxItemStackCount = 64`. If the full requested count cannot fit, the method
returns false and leaves inventory unchanged. This method is atomic: partial
adds are not allowed.

`consume_selected` only affects the selected hotbar slot. It returns false if
the selected slot is empty or has fewer than the requested count. When count
reaches zero, the slot is cleared.

Both inventory mutation methods reject `count == 0` by returning false and
leaving inventory unchanged. This makes accidental no-op calls visible in tests
instead of silently succeeding.

### Block Interaction

`engine/gameplay/BlockInteraction` gets survival-aware helpers while preserving
the existing world mutation boundary:

- Breaking checks the target block's drop item before mutating the world.
- Air break is a no-op. Water may keep any existing removal behavior for this
  slice, but it never creates or consumes inventory.
- If the block has a drop and inventory cannot accept it, the break is rejected.
- If the break mutation succeeds, the matching item is added to inventory.
- Placement first checks that the selected item maps to a placeable block type,
  then applies the world mutation, then consumes one item only if the mutation
  succeeded.
- If the selected item does not map to a placeable block, placement is rejected
  and inventory remains unchanged.

This ordering prevents lost items and prevents drops from being created when the
world mutation fails.

### Engine Wiring

`Engine` should run the normal game path as survival-light rather than creative
inventory mode:

- New player inventories start empty.
- Loaded inventories use the existing `InventorySnapshot`.
- Creative or developer inventory seeding must remain an explicit alternate
  path, not the default survival-light game path.
- Existing save/load paths remain unchanged unless tests reveal a gap.

The current `player.dat` format stores the hotbar and selected slot, so the
first slice does not require a save format migration.

## Data Rules

Drop mapping is intentionally direct:

| Block | Drop |
| --- | --- |
| Stone | Stone x1 |
| Dirt | Dirt x1 |
| Grass | Grass x1 |
| Air | none |
| Water | none |

Block IDs already map cleanly to item IDs for existing blocks, so no general
loot table is required for this slice. If later features need richer drops, this
can grow into a `BlockDropTable` without changing the player-facing behavior.

Air and Water are non-collectible. Breaking them never adds inventory. Air break
is a no-op. If current interaction rules allow removing Water, that behavior may
remain unchanged for this slice, but it must not mutate inventory.

## Error Handling

- Full inventory: reject break, leave block in world.
- Empty selected hotbar slot: reject placement, leave world unchanged.
- Selected item is not placeable: reject placement, leave inventory unchanged.
- Placement mutation rejected: leave inventory unchanged.
- Break mutation rejected: leave inventory unchanged.
- Unknown or non-collectible block: do not add an item.
- Inventory mutation with `count == 0`: reject and leave inventory unchanged.

These failures should be deterministic and test-covered. User-facing feedback
can remain minimal for this slice; logs are enough unless the existing UI has a
natural place for a short message.

## Testing

Add or update focused tests for:

- Inventory adds items into an existing compatible stack.
- Inventory uses an empty slot when the compatible stack is full.
- Inventory rejects additions that cannot fully fit and leaves all existing
  stacks unchanged.
- Inventory rejects `count == 0` for add and consume operations.
- Consuming the selected stack decrements by one.
- Consuming the final item clears the selected slot.
- Placement rejects selected items that do not map to placeable blocks.
- Placement consumes one item only when the world mutation succeeds.
- Failed placement consumes nothing.
- Breaking Stone/Dirt/Grass adds exactly one matching item and removes the
  block.
- Breaking with no inventory capacity leaves the block in place.
- Save/load continues to preserve hotbar counts and selected slot.

## Acceptance Criteria

- A new or reset player can gather Stone/Dirt/Grass by breaking blocks.
- The hotbar count increases after successful collection.
- Placing from a hotbar stack decrements the count.
- Placing with an empty selected slot does nothing.
- A full inventory cannot silently destroy drops.
- Existing tests pass, and new survival-loop tests cover the resource accounting
  rules.
