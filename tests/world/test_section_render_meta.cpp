#include <catch2/catch_test_macros.hpp>

#include "engine/gameplay/BlockRegistry.hpp"
#include "engine/gameplay/BlockState.hpp"
#include "engine/world/Section.hpp"
#include "engine/world/SectionIndexing.hpp"

TEST_CASE("render meta air palette is empty") {
    engine::Section section;
    section.recompute_render_meta();
    REQUIRE(section.render_meta.is_empty);
    REQUIRE_FALSE(section.render_meta.is_opaque_full);
    REQUIRE(section.render_meta.face_solid_mask == 0);
}

TEST_CASE("render meta uniform stone is opaque full with all faces solid") {
    engine::Section section;
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int y = 0; y < engine::SECTION_DIM; ++y) {
        for (int z = 0; z < engine::SECTION_DIM; ++z) {
            for (int x = 0; x < engine::SECTION_DIM; ++x) {
                REQUIRE(section.write_block(x, y, z, stone));
            }
        }
    }
    section.sync_occupancy_from_blocks();
    REQUIRE_FALSE(section.render_meta.is_empty);
    REQUIRE(section.render_meta.is_opaque_full);
    REQUIRE(section.render_meta.face_solid_mask == 0x3F);
}

TEST_CASE("render meta water only is not empty") {
    engine::Section section;
    const engine::BlockState water = engine::make_block_state(engine::BLOCK_WATER, 0);
    REQUIRE(section.write_block(0, 0, 0, water));
    section.sync_occupancy_from_blocks();
    REQUIRE_FALSE(section.render_meta.is_empty);
    REQUIRE_FALSE(section.render_meta.is_opaque_full);
}

TEST_CASE("render meta mixed section sets single face solid bit") {
    engine::Section section;
    const engine::BlockState stone = engine::make_block_state(engine::BLOCK_STONE, 0);
    for (int z = 0; z < engine::SECTION_DIM; ++z) {
        for (int x = 0; x < engine::SECTION_DIM; ++x) {
            REQUIRE(section.write_block(x, 0, z, stone));
        }
    }
    section.sync_occupancy_from_blocks();
    REQUIRE(engine::face_solid(section.render_meta, engine::Face::NY));
    REQUIRE_FALSE(engine::face_solid(section.render_meta, engine::Face::PY));
}