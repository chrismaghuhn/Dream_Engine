#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/world/OriginRebase.hpp"
#include "engine/world/WorldEvents.hpp"
#include "engine/world/WorldModule.hpp"
#include "engine/world/WorldPosition.hpp"

#include <glm/glm.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE("WorldPosition from_world_blocks roundtrip") {
    const engine::WorldPosition pos = engine::WorldPosition::from_world_blocks(-1, 64, 31);
    REQUIRE(pos.chunk == engine::ChunkCoord{-1, 2, 0});
    REQUIRE_THAT(pos.local.x, WithinAbs(31.f, 0.001f));
    REQUIRE_THAT(pos.local.y, WithinAbs(0.f, 0.001f));
    REQUIRE_THAT(pos.local.z, WithinAbs(31.f, 0.001f));
    REQUIRE(pos.to_world_blocks() == glm::ivec3{-1, 64, 31});
}

TEST_CASE("origin rebase keeps WorldPosition chunk and local stable") {
    engine::WorldPosition pos = engine::WorldPosition::from_world_blocks(600, 0, 0);
    const engine::ChunkCoord chunk_before = pos.chunk;
    const glm::vec3 local_before = pos.local;

    flecs::world world;
    world.import<engine::WorldModule>();

    engine::WorldConfig config{};
    config.rebase_radius = 512.f;

    engine::OriginRebase rebase;
    const bool shifted = rebase.maybe_rebase(world, glm::vec3{600.f, 0.f, 0.f}, config);

    REQUIRE(shifted);
    REQUIRE(pos.chunk == chunk_before);
    REQUIRE_THAT(pos.local.x, WithinAbs(local_before.x, 0.001f));
    REQUIRE_THAT(pos.local.y, WithinAbs(local_before.y, 0.001f));
    REQUIRE_THAT(pos.local.z, WithinAbs(local_before.z, 0.001f));
}

TEST_CASE("origin rebase shifts render_origin by expected delta") {
    flecs::world world;
    world.import<engine::WorldModule>();

    engine::WorldConfig config{};
    config.rebase_radius = 512.f;

    engine::OriginRebase rebase;
    REQUIRE(rebase.render_origin() == glm::vec3{0.f});

    const bool shifted = rebase.maybe_rebase(world, glm::vec3{600.f, 0.f, 0.f}, config);
    REQUIRE(shifted);
    REQUIRE_THAT(rebase.render_origin().x, WithinAbs(512.f, 0.001f));
    REQUIRE_THAT(rebase.render_origin().y, WithinAbs(0.f, 0.001f));
    REQUIRE_THAT(rebase.render_origin().z, WithinAbs(0.f, 0.001f));
}

TEST_CASE("origin rebase emits EvtOriginShift without ChunkDirty") {
    flecs::world world;
    world.import<engine::WorldModule>();

    int origin_shift_count = 0;
    glm::vec3 observed_origin{};
    world.observer()
        .event<engine::EvtOriginShift>()
        .with<engine::WorldRoot>()
        .run([&](flecs::iter& it) {
            while (it.next()) {
                if (const auto* evt = it.param<engine::EvtOriginShift>()) {
                    ++origin_shift_count;
                    observed_origin = evt->new_origin;
                }
            }
        });

    int chunk_dirty_count = 0;
    world.observer<engine::ChunkDirty>()
        .event(flecs::OnAdd)
        .each([&](flecs::entity, engine::ChunkDirty) { ++chunk_dirty_count; });

    engine::WorldConfig config{};
    config.rebase_radius = 512.f;

    engine::OriginRebase rebase;
    REQUIRE(rebase.maybe_rebase(world, glm::vec3{600.f, 0.f, 0.f}, config));

    REQUIRE(origin_shift_count == 1);
    REQUIRE_THAT(observed_origin.x, WithinAbs(512.f, 0.001f));
    REQUIRE(chunk_dirty_count == 0);
}

TEST_CASE("origin rebase skipped within radius") {
    flecs::world world;
    world.import<engine::WorldModule>();

    int origin_shift_count = 0;
    world.observer()
        .event<engine::EvtOriginShift>()
        .with<engine::WorldRoot>()
        .run([&](flecs::iter& it) {
            while (it.next()) {
                if (it.param<engine::EvtOriginShift>()) {
                    ++origin_shift_count;
                }
            }
        });

    engine::WorldConfig config{};
    config.rebase_radius = 512.f;

    engine::OriginRebase rebase;
    REQUIRE_FALSE(rebase.maybe_rebase(world, glm::vec3{400.f, 0.f, 0.f}, config));
    REQUIRE(rebase.render_origin() == glm::vec3{0.f});
    REQUIRE(origin_shift_count == 0);
}
