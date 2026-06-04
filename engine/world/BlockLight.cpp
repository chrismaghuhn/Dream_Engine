#include "engine/world/BlockLight.hpp"



#include "engine/gameplay/BlockRegistry.hpp"

#include "engine/world/Chunk.hpp"

#include "engine/world/Section.hpp"



#include <algorithm>

#include <array>

#include <cstdint>

#include <deque>

#include <unordered_set>



namespace engine {

namespace {



struct SectionKeyHash {

    size_t operator()(const SectionLightTarget& t) const noexcept {

        size_t h = std::hash<int>()(t.chunk.x);

        h ^= std::hash<int>()(t.chunk.y) + 0x9e3779b9 + (h << 6) + (h >> 2);

        h ^= std::hash<int>()(t.chunk.z) + 0x9e3779b9 + (h << 6) + (h >> 2);

        h ^= std::hash<int>()(t.section.x) + 0x9e3779b9 + (h << 6) + (h >> 2);

        h ^= std::hash<int>()(t.section.y) + 0x9e3779b9 + (h << 6) + (h >> 2);

        h ^= std::hash<int>()(t.section.z) + 0x9e3779b9 + (h << 6) + (h >> 2);

        return h;

    }

};



struct SectionKeyEq {

    bool operator()(const SectionLightTarget& a, const SectionLightTarget& b) const noexcept {

        return a.chunk == b.chunk && a.section == b.section;

    }

};



[[nodiscard]] Section* section_at(ChunkStore& store, ChunkCoord chunk, glm::ivec3 section_coord) {

    Chunk* c = store.try_get(chunk);

    if (!c) {

        return nullptr;

    }

    if (section_coord.x < 0 || section_coord.x >= CHUNK_SECTIONS_PER_AXIS

        || section_coord.y < 0 || section_coord.y >= CHUNK_SECTIONS_PER_AXIS

        || section_coord.z < 0 || section_coord.z >= CHUNK_SECTIONS_PER_AXIS) {

        return nullptr;

    }

    return &c->section_at(section_coord);

}

[[nodiscard]] bool in_section(int x, int y, int z) {

    return x >= 0 && x < SECTION_DIM && y >= 0 && y < SECTION_DIM && z >= 0 && z < SECTION_DIM;

}



[[nodiscard]] glm::ivec3 local_on_face(Face face, int u, int v) {

    switch (face) {

    case Face::NX:

        return {0, u, v};

    case Face::PX:

        return {SECTION_DIM - 1, u, v};

    case Face::NY:

        return {u, 0, v};

    case Face::PY:

        return {u, SECTION_DIM - 1, v};

    case Face::NZ:

        return {u, v, 0};

    case Face::PZ:

        return {u, v, SECTION_DIM - 1};

    }

    return {};

}



[[nodiscard]] glm::ivec3 neighbor_local_on_face(Face face, int u, int v) {

    switch (face) {

    case Face::NX:

        return {SECTION_DIM - 1, u, v};

    case Face::PX:

        return {0, u, v};

    case Face::NY:

        return {u, SECTION_DIM - 1, v};

    case Face::PY:

        return {u, 0, v};

    case Face::NZ:

        return {u, v, SECTION_DIM - 1};

    case Face::PZ:

        return {u, v, 0};

    }

    return {};

}



void sort_targets(std::vector<SectionLightTarget>& targets) {

    std::sort(targets.begin(), targets.end(), [](const SectionLightTarget& a, const SectionLightTarget& b) {

        if (a.chunk.x != b.chunk.x) {

            return a.chunk.x < b.chunk.x;

        }

        if (a.chunk.y != b.chunk.y) {

            return a.chunk.y < b.chunk.y;

        }

        if (a.chunk.z != b.chunk.z) {

            return a.chunk.z < b.chunk.z;

        }

        if (a.section.x != b.section.x) {

            return a.section.x < b.section.x;

        }

        if (a.section.y != b.section.y) {

            return a.section.y < b.section.y;

        }

        return a.section.z < b.section.z;

    });

}



void enqueue_unique(

    BlockLightUpdateQueue& queue,

    std::unordered_set<SectionLightTarget, SectionKeyHash, SectionKeyEq>& seen,

    ChunkCoord chunk,

    glm::ivec3 section_coord) {

    const SectionLightTarget key{chunk, section_coord};

    if (!seen.insert(key).second) {

        return;

    }

    queue.enqueue(chunk, section_coord);

}



void seed_from_neighbor_face(

    ChunkStore& store,

    Section& section,

    ChunkCoord chunk,

    glm::ivec3 section_coord,

    Face face,

    std::deque<glm::ivec3>& bfs) {

    ChunkCoord neighbor_chunk{};

    glm::ivec3 neighbor_section_coord{};

    neighbor_chunk_and_section(chunk, section_coord, face, neighbor_chunk, neighbor_section_coord);



    Section* neighbor = section_at(store, neighbor_chunk, neighbor_section_coord);

    if (!neighbor) {

        return;

    }



    for (int u = 0; u < SECTION_DIM; ++u) {

        for (int v = 0; v < SECTION_DIM; ++v) {

            const glm::ivec3 n_local = neighbor_local_on_face(face, u, v);

            const size_t n_idx =

                static_cast<size_t>(block_index(n_local.x, n_local.y, n_local.z));

            const uint8_t neighbor_level = neighbor->block_light[n_idx];

            if (neighbor_level <= 1) {

                continue;

            }



            const glm::ivec3 local = local_on_face(face, u, v);

            const size_t idx = static_cast<size_t>(block_index(local.x, local.y, local.z));

            const BlockState state = section.read_block(local.x, local.y, local.z);

            if (blocks_light(block_id(state))) {

                continue;

            }



            const uint8_t incoming = static_cast<uint8_t>(neighbor_level - 1);

            if (incoming > section.block_light[idx]) {

                section.block_light[idx] = incoming;

                bfs.push_back(local);

            }

        }

    }

}



void propagate_to_neighbor_face(

    ChunkStore& store,

    BlockLightUpdateQueue& queue,

    std::unordered_set<SectionLightTarget, SectionKeyHash, SectionKeyEq>& seen,

    Section& section,

    ChunkCoord chunk,

    glm::ivec3 section_coord,

    Face face,

    int local_x,

    int local_y,

    int local_z,

    uint8_t level) {

    if (level <= 1) {

        return;

    }



    ChunkCoord neighbor_chunk{};

    glm::ivec3 neighbor_section_coord{};

    neighbor_chunk_and_section(chunk, section_coord, face, neighbor_chunk, neighbor_section_coord);



    Section* neighbor = section_at(store, neighbor_chunk, neighbor_section_coord);

    if (!neighbor) {

        return;

    }



    int u = 0;

    int v = 0;

    glm::ivec3 n_local{};

    switch (face) {

    case Face::NX:

        u = local_y;

        v = local_z;

        n_local = {SECTION_DIM - 1, local_y, local_z};

        break;

    case Face::PX:

        u = local_y;

        v = local_z;

        n_local = {0, local_y, local_z};

        break;

    case Face::NY:

        u = local_x;

        v = local_z;

        n_local = {local_x, SECTION_DIM - 1, local_z};

        break;

    case Face::PY:

        u = local_x;

        v = local_z;

        n_local = {local_x, 0, local_z};

        break;

    case Face::NZ:

        u = local_x;

        v = local_y;

        n_local = {local_x, local_y, SECTION_DIM - 1};

        break;

    case Face::PZ:

        u = local_x;

        v = local_y;

        n_local = {local_x, local_y, 0};

        break;

    }



    (void)u;

    (void)v;



    const size_t n_idx = static_cast<size_t>(block_index(n_local.x, n_local.y, n_local.z));

    const BlockState n_state = neighbor->read_block(n_local.x, n_local.y, n_local.z);

    if (blocks_light(block_id(n_state))) {

        return;

    }



    const uint8_t propagated = static_cast<uint8_t>(level - 1);

    if (propagated > neighbor->block_light[n_idx]) {

        neighbor->block_light[n_idx] = propagated;

        enqueue_unique(queue, seen, neighbor_chunk, neighbor_section_coord);

    }

}



void try_relax(

    ChunkStore& store,

    BlockLightUpdateQueue& queue,

    std::unordered_set<SectionLightTarget, SectionKeyHash, SectionKeyEq>& seen,

    Section& section,

    ChunkCoord chunk,

    glm::ivec3 section_coord,

    int x,

    int y,

    int z,

    uint8_t level,

    std::deque<glm::ivec3>& bfs) {

    if (level == 0) {

        return;

    }



    const int nx = x + 1;

    if (in_section(nx, y, z)) {

        const BlockState n_state = section.read_block(nx, y, z);

        if (!blocks_light(block_id(n_state))) {

            const size_t n_idx = static_cast<size_t>(block_index(nx, y, z));

            const uint8_t next = static_cast<uint8_t>(level - 1);

            if (next > section.block_light[n_idx]) {

                section.block_light[n_idx] = next;

                bfs.push_back({nx, y, z});

            }

        }

    } else if (x == SECTION_DIM - 1) {

        propagate_to_neighbor_face(

            store, queue, seen, section, chunk, section_coord, Face::PX, x, y, z, level);

    }



    const int px = x - 1;

    if (in_section(px, y, z)) {

        const BlockState n_state = section.read_block(px, y, z);

        if (!blocks_light(block_id(n_state))) {

            const size_t n_idx = static_cast<size_t>(block_index(px, y, z));

            const uint8_t next = static_cast<uint8_t>(level - 1);

            if (next > section.block_light[n_idx]) {

                section.block_light[n_idx] = next;

                bfs.push_back({px, y, z});

            }

        }

    } else if (x == 0) {

        propagate_to_neighbor_face(

            store, queue, seen, section, chunk, section_coord, Face::NX, x, y, z, level);

    }



    const int ny = y + 1;

    if (in_section(x, ny, z)) {

        const BlockState n_state = section.read_block(x, ny, z);

        if (!blocks_light(block_id(n_state))) {

            const size_t n_idx = static_cast<size_t>(block_index(x, ny, z));

            const uint8_t next = static_cast<uint8_t>(level - 1);

            if (next > section.block_light[n_idx]) {

                section.block_light[n_idx] = next;

                bfs.push_back({x, ny, z});

            }

        }

    } else if (y == SECTION_DIM - 1) {

        propagate_to_neighbor_face(

            store, queue, seen, section, chunk, section_coord, Face::PY, x, y, z, level);

    }



    const int py = y - 1;

    if (in_section(x, py, z)) {

        const BlockState n_state = section.read_block(x, py, z);

        if (!blocks_light(block_id(n_state))) {

            const size_t n_idx = static_cast<size_t>(block_index(x, py, z));

            const uint8_t next = static_cast<uint8_t>(level - 1);

            if (next > section.block_light[n_idx]) {

                section.block_light[n_idx] = next;

                bfs.push_back({x, py, z});

            }

        }

    } else if (y == 0) {

        propagate_to_neighbor_face(

            store, queue, seen, section, chunk, section_coord, Face::NY, x, y, z, level);

    }



    const int nz = z + 1;

    if (in_section(x, y, nz)) {

        const BlockState n_state = section.read_block(x, y, nz);

        if (!blocks_light(block_id(n_state))) {

            const size_t n_idx = static_cast<size_t>(block_index(x, y, nz));

            const uint8_t next = static_cast<uint8_t>(level - 1);

            if (next > section.block_light[n_idx]) {

                section.block_light[n_idx] = next;

                bfs.push_back({x, y, nz});

            }

        }

    } else if (z == SECTION_DIM - 1) {

        propagate_to_neighbor_face(

            store, queue, seen, section, chunk, section_coord, Face::PZ, x, y, z, level);

    }



    const int pz = z - 1;

    if (in_section(x, y, pz)) {

        const BlockState n_state = section.read_block(x, y, pz);

        if (!blocks_light(block_id(n_state))) {

            const size_t n_idx = static_cast<size_t>(block_index(x, y, pz));

            const uint8_t next = static_cast<uint8_t>(level - 1);

            if (next > section.block_light[n_idx]) {

                section.block_light[n_idx] = next;

                bfs.push_back({x, y, pz});

            }

        }

    } else if (z == 0) {

        propagate_to_neighbor_face(

            store, queue, seen, section, chunk, section_coord, Face::NZ, x, y, z, level);

    }

}



} // namespace

void neighbor_chunk_and_section(ChunkCoord chunk, glm::ivec3 section_coord, Face face,
                                ChunkCoord& out_chunk, glm::ivec3& out_section) {
    out_chunk   = chunk;
    out_section = section_coord;

    switch (face) {
    case Face::PX:
        if (section_coord.x >= CHUNK_SECTIONS_PER_AXIS - 1) {
            out_chunk.x += 1;
            out_section.x = 0;
        } else {
            out_section.x += 1;
        }
        break;
    case Face::NX:
        if (section_coord.x <= 0) {
            out_chunk.x -= 1;
            out_section.x = CHUNK_SECTIONS_PER_AXIS - 1;
        } else {
            out_section.x -= 1;
        }
        break;
    case Face::PY:
        if (section_coord.y >= CHUNK_SECTIONS_PER_AXIS - 1) {
            out_chunk.y += 1;
            out_section.y = 0;
        } else {
            out_section.y += 1;
        }
        break;
    case Face::NY:
        if (section_coord.y <= 0) {
            out_chunk.y -= 1;
            out_section.y = CHUNK_SECTIONS_PER_AXIS - 1;
        } else {
            out_section.y -= 1;
        }
        break;
    case Face::PZ:
        if (section_coord.z >= CHUNK_SECTIONS_PER_AXIS - 1) {
            out_chunk.z += 1;
            out_section.z = 0;
        } else {
            out_section.z += 1;
        }
        break;
    case Face::NZ:
        if (section_coord.z <= 0) {
            out_chunk.z -= 1;
            out_section.z = CHUNK_SECTIONS_PER_AXIS - 1;
        } else {
            out_section.z -= 1;
        }
        break;
    }
}

void BlockLightUpdateQueue::enqueue(ChunkCoord chunk, glm::ivec3 section) {

    pending_.push_back(SectionLightTarget{chunk, section});

}



void BlockLightUpdateQueue::clear() {

    pending_.clear();

}



bool BlockLightUpdateQueue::drain_one(ChunkStore& store) {

    if (pending_.empty()) {

        return false;

    }



    sort_targets(pending_);

    const SectionLightTarget target = pending_.front();

    pending_.erase(pending_.begin());



    flood_section_block_light(store, *this, target.chunk, target.section);

    return true;

}



void BlockLightUpdateQueue::drain_all(ChunkStore& store) {

    std::unordered_set<SectionLightTarget, SectionKeyHash, SectionKeyEq> seen{};

    while (!pending_.empty()) {

        sort_targets(pending_);

        const SectionLightTarget target = pending_.front();

        pending_.erase(pending_.begin());

        if (!seen.insert(target).second) {

            continue;

        }

        flood_section_block_light(store, *this, target.chunk, target.section);

    }

}



Section* neighbor_section(

    ChunkStore& store,

    ChunkCoord chunk,

    glm::ivec3 section_coord,

    Face face) {

    ChunkCoord neighbor_chunk{};

    glm::ivec3 neighbor_section_coord{};

    neighbor_chunk_and_section(chunk, section_coord, face, neighbor_chunk, neighbor_section_coord);

    return section_at(store, neighbor_chunk, neighbor_section_coord);

}



void refresh_section_border_cache(ChunkStore& store, ChunkCoord chunk, glm::ivec3 section_coord) {

    Section* section = section_at(store, chunk, section_coord);

    if (!section) {

        return;

    }



    const BlockState air = make_block_state(BLOCK_AIR, 0);

    const std::array<Face, 6> faces = {

        Face::NX, Face::PX, Face::NY, Face::PY, Face::NZ, Face::PZ,

    };



    for (const Face face : faces) {

        ChunkCoord neighbor_chunk{};

        glm::ivec3 neighbor_section_coord{};

        neighbor_chunk_and_section(chunk, section_coord, face, neighbor_chunk, neighbor_section_coord);



        Section* neighbor = section_at(store, neighbor_chunk, neighbor_section_coord);

        auto& border_cells = section->border.face[static_cast<size_t>(face)];



        for (int u = 0; u < SECTION_DIM; ++u) {

            for (int v = 0; v < SECTION_DIM; ++v) {

                const size_t border_idx = static_cast<size_t>(u + SECTION_DIM * v);

                BorderCell& cell = border_cells[border_idx];



                if (!neighbor) {

                    cell.block       = air;

                    cell.sky_light   = 0;

                    cell.block_light = 0;

                    continue;

                }



                const glm::ivec3 n_local = neighbor_local_on_face(face, u, v);

                const size_t n_idx =

                    static_cast<size_t>(block_index(n_local.x, n_local.y, n_local.z));

                cell.block       = neighbor->read_block(n_local.x, n_local.y, n_local.z);

                cell.sky_light   = neighbor->sky_light[n_idx];

                cell.block_light = neighbor->block_light[n_idx];

            }

        }

    }



    section->border.dirty = false;



    for (const Face face : faces) {

        ChunkCoord neighbor_chunk{};

        glm::ivec3 neighbor_section_coord{};

        neighbor_chunk_and_section(chunk, section_coord, face, neighbor_chunk, neighbor_section_coord);

        if (Section* neighbor = section_at(store, neighbor_chunk, neighbor_section_coord)) {

            neighbor->border.dirty = true;

        }

    }

}



void flood_section_block_light(ChunkStore& store, BlockLightUpdateQueue& queue, ChunkCoord chunk,

                               glm::ivec3 section_coord) {

    Section* section = section_at(store, chunk, section_coord);

    if (!section) {

        return;

    }



    std::unordered_set<SectionLightTarget, SectionKeyHash, SectionKeyEq> seen{};

    std::deque<glm::ivec3> bfs{};



    for (int y = 0; y < SECTION_DIM; ++y) {

        for (int z = 0; z < SECTION_DIM; ++z) {

            for (int x = 0; x < SECTION_DIM; ++x) {

                const size_t idx = static_cast<size_t>(block_index(x, y, z));

                const BlockState state = section->read_block(x, y, z);

                const uint8_t emission = light_emission(block_id(state));

                section->block_light[idx] = emission;

                if (emission > 0) {

                    bfs.push_back({x, y, z});

                }

            }

        }

    }



    const std::array<Face, 6> faces = {

        Face::NX, Face::PX, Face::NY, Face::PY, Face::NZ, Face::PZ,

    };

    for (const Face face : faces) {

        seed_from_neighbor_face(store, *section, chunk, section_coord, face, bfs);

    }



    while (!bfs.empty()) {

        const glm::ivec3 pos = bfs.front();

        bfs.pop_front();



        const size_t idx = static_cast<size_t>(block_index(pos.x, pos.y, pos.z));

        const uint8_t level = section->block_light[idx];

        if (level == 0) {

            continue;

        }



        try_relax(store, queue, seen, *section, chunk, section_coord, pos.x, pos.y, pos.z, level, bfs);

    }



    refresh_section_border_cache(store, chunk, section_coord);

}



std::vector<ChunkCoord> on_block_light_block_changed(

    ChunkStore& store,

    BlockLightUpdateQueue& queue,

    BlockPos pos,

    BlockState old_state,

    BlockState new_state) {

    if (!block_change_affects_light(old_state, new_state)) {

        return {};

    }



    const glm::ivec3 sec = pos.section_coord();

    const glm::ivec3 blk = pos.block_in_section();



    queue.clear();

    queue.enqueue(pos.chunk, sec);



    if (blk.x == 0) {

        ChunkCoord nc{};

        glm::ivec3 ns{};

        neighbor_chunk_and_section(pos.chunk, sec, Face::NX, nc, ns);

        queue.enqueue(nc, ns);

    }

    if (blk.x == SECTION_DIM - 1) {

        ChunkCoord nc{};

        glm::ivec3 ns{};

        neighbor_chunk_and_section(pos.chunk, sec, Face::PX, nc, ns);

        queue.enqueue(nc, ns);

    }

    if (blk.y == 0) {

        ChunkCoord nc{};

        glm::ivec3 ns{};

        neighbor_chunk_and_section(pos.chunk, sec, Face::NY, nc, ns);

        queue.enqueue(nc, ns);

    }

    if (blk.y == SECTION_DIM - 1) {

        ChunkCoord nc{};

        glm::ivec3 ns{};

        neighbor_chunk_and_section(pos.chunk, sec, Face::PY, nc, ns);

        queue.enqueue(nc, ns);

    }

    if (blk.z == 0) {

        ChunkCoord nc{};

        glm::ivec3 ns{};

        neighbor_chunk_and_section(pos.chunk, sec, Face::NZ, nc, ns);

        queue.enqueue(nc, ns);

    }

    if (blk.z == SECTION_DIM - 1) {

        ChunkCoord nc{};

        glm::ivec3 ns{};

        neighbor_chunk_and_section(pos.chunk, sec, Face::PZ, nc, ns);

        queue.enqueue(nc, ns);

    }



    queue.drain_all(store);

    std::vector<ChunkCoord> dirty_chunks{};
    dirty_chunks.push_back(pos.chunk);

    const std::array<Face, 6> faces = {

        Face::NX, Face::PX, Face::NY, Face::PY, Face::NZ, Face::PZ,

    };

    for (const Face face : faces) {

        ChunkCoord nc{};

        glm::ivec3 ns{};

        neighbor_chunk_and_section(pos.chunk, sec, face, nc, ns);

        if (section_at(store, nc, ns)) {

            dirty_chunks.push_back(nc);

        }

    }



    std::sort(dirty_chunks.begin(), dirty_chunks.end(), [](const ChunkCoord& a, const ChunkCoord& b) {

        if (a.x != b.x) {

            return a.x < b.x;

        }

        if (a.y != b.y) {

            return a.y < b.y;

        }

        return a.z < b.z;

    });

    dirty_chunks.erase(std::unique(dirty_chunks.begin(), dirty_chunks.end(),

                                   [](const ChunkCoord& a, const ChunkCoord& b) {

                                       return a.x == b.x && a.y == b.y && a.z == b.z;

                                   }),

                       dirty_chunks.end());



    return dirty_chunks;

}



} // namespace engine

