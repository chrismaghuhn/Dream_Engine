#include "engine/world/ChunkStore.hpp"

#include "engine/gameplay/BlockRegistry.hpp"

namespace engine {

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
    ++slot.generation;
    slot.chunk = Chunk{};
    coord_to_slot_.erase(it);
    free_list_.push_back(slot_id);
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

bool ChunkStore::write_block(BlockPos pos, BlockState state) {
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
