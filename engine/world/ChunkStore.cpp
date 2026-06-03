#include "engine/world/ChunkStore.hpp"

#include "engine/world/BlockLight.hpp"

#include "engine/gameplay/BlockRegistry.hpp"

namespace engine {

void ChunkStore::init(uint32_t max_loaded_chunks) {
    max_loaded_chunks_ = max_loaded_chunks;
}

uint32_t ChunkStore::loaded_count() const {
    return static_cast<uint32_t>(coord_to_slot_.size());
}

const Chunk* ChunkStore::try_get(ChunkCoord coord) const {
    const auto it = coord_to_slot_.find(coord);
    if (it == coord_to_slot_.end()) {
        return nullptr;
    }
    const ChunkSlot& slot = slots_[it->second];
    if (!slot.occupied) {
        return nullptr;
    }
    return &slot.chunk;
}

Chunk* ChunkStore::try_get(ChunkCoord coord) {
    return const_cast<Chunk*>(static_cast<const ChunkStore*>(this)->try_get(coord));
}

Chunk* ChunkStore::allocate(ChunkCoord coord) {
    if (coord_to_slot_.contains(coord)) {
        return try_get(coord);
    }

    if (max_loaded_chunks_ > 0 && loaded_count() >= max_loaded_chunks_) {
        return nullptr;
    }

    uint32_t slot_id = 0;
    if (!free_list_.empty()) {
        slot_id = free_list_.back();
        free_list_.pop_back();
    } else {
        slot_id = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
    }

    ChunkSlot& slot = slots_[slot_id];
    slot.occupied = true;
    ++slot.generation;
    slot.chunk = Chunk{};
    slot.chunk.coord = coord;
    coord_to_slot_[coord] = slot_id;
    return &slot.chunk;
}

void ChunkStore::free(ChunkCoord coord) {
    const auto it = coord_to_slot_.find(coord);
    if (it == coord_to_slot_.end()) {
        return;
    }

    const uint32_t slot_id = it->second;
    ChunkSlot& slot = slots_[slot_id];
    slot.occupied = false;
    slot.pending_unload = false;
    ++slot.generation;
    slot.chunk = Chunk{};
    coord_to_slot_.erase(it);
    coord_to_entity_.erase(coord);
    free_list_.push_back(slot_id);
}

ChunkSlotRef ChunkStore::slot_ref_for(ChunkCoord coord) const {
    const auto it = coord_to_slot_.find(coord);
    if (it == coord_to_slot_.end()) {
        return {};
    }
    const ChunkSlot& slot = slots_[it->second];
    return { it->second, slot.generation };
}

bool ChunkStore::validate_slot_ref(ChunkSlotRef ref) const {
    if (ref.slot_id >= slots_.size()) {
        return false;
    }
    const ChunkSlot& slot = slots_[ref.slot_id];
    if (!slot.occupied) {
        return false;
    }
    return slot.generation == ref.generation;
}

Chunk* ChunkStore::try_get_via_ref(ChunkSlotRef ref) {
    return const_cast<Chunk*>(static_cast<const ChunkStore*>(this)->try_get_via_ref(ref));
}

const Chunk* ChunkStore::try_get_via_ref(ChunkSlotRef ref) const {
    if (!validate_slot_ref(ref)) {
        return nullptr;
    }
    return &slots_[ref.slot_id].chunk;
}

bool ChunkStore::is_pending_unload(ChunkCoord coord) const {
    const auto it = coord_to_slot_.find(coord);
    if (it == coord_to_slot_.end()) {
        return false;
    }
    return slots_[it->second].pending_unload;
}

void ChunkStore::set_pending_unload(ChunkCoord coord, bool value) {
    const auto it = coord_to_slot_.find(coord);
    if (it == coord_to_slot_.end()) {
        return;
    }
    slots_[it->second].pending_unload = value;
}

uint64_t ChunkStore::entity_for(ChunkCoord coord) const {
    const auto it = coord_to_entity_.find(coord);
    if (it == coord_to_entity_.end()) {
        return 0;
    }
    return it->second;
}

void ChunkStore::set_entity_for(ChunkCoord coord, uint64_t entity_id) {
    coord_to_entity_[coord] = entity_id;
}

void ChunkStore::clear_entity_for(ChunkCoord coord) {
    coord_to_entity_.erase(coord);
}

BlockState ChunkStore::read_block(BlockPos pos) const {
    const Chunk* chunk = try_get(pos.chunk);
    if (!chunk) {
        return make_block_state(BLOCK_AIR, 0);
    }

    const glm::ivec3 sec = pos.section_coord();
    const glm::ivec3 blk = pos.block_in_section();
    return chunk->section_at(sec).read_block(blk.x, blk.y, blk.z);
}

bool ChunkStore::write_block(BlockPos pos, BlockState state,
                             std::vector<ChunkCoord>* light_dirty_chunks) {
    Chunk* chunk = try_get(pos.chunk);
    if (!chunk) {
        return false;
    }

    const glm::ivec3 sec = pos.section_coord();
    const glm::ivec3 blk = pos.block_in_section();
    Section& section = chunk->section_at(sec);

    const BlockState old = section.read_block(blk.x, blk.y, blk.z);
    if (!section.write_block(blk.x, blk.y, blk.z, state)) {
        return false;
    }

    const bool was_solid = is_solid(block_id(old));
    const bool now_solid = is_solid(block_id(state));
    if (was_solid != now_solid) {
        section.occupancy.set(blk.x, blk.y, blk.z, now_solid);
    }

    BlockLightUpdateQueue light_queue{};
    const std::vector<ChunkCoord> dirty =
        on_block_light_block_changed(*this, light_queue, pos, old, state);
    if (light_dirty_chunks != nullptr) {
        *light_dirty_chunks = dirty;
    }

    return true;
}

bool occupancy_at(int wx, int wy, int wz, ChunkStore& store, OccupancyPolicy policy) {
    const ChunkCoord cc = block_to_chunk(wx, wy, wz);
    const Chunk* chunk = store.try_get(cc);
    if (!chunk) {
        return policy == OccupancyPolicy::SolidIfChunkMissing;
    }

    const glm::ivec3 local = block_local_in_chunk(wx, wy, wz);
    const glm::ivec3 sec = { local.x >> 4, local.y >> 4, local.z >> 4 };
    const glm::ivec3 blk = { local.x & 15, local.y & 15, local.z & 15 };
    return chunk->section_at(sec).occupancy_bit(blk);
}

} // namespace engine
