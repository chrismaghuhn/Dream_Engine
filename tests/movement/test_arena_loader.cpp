#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "engine/movement/core/ArenaLoader.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/MovementWorld.hpp"
#include "engine/movement/core/PersistentId.hpp"

using Catch::Matchers::ContainsSubstring;
using namespace engine::movement;

namespace {
constexpr const char* kArena = R"(
arena "movement_test" version 1
{
    entity "player"
    {
        transform position 0 1.1 0 yaw 0
        player_controller speed 4.0 jump_velocity 7.0 gravity 18.0
        collider capsule radius 0.4 height 1.8
        camera_rig yaw 0 pitch -15 distance 5.0 height 1.7
    }

    entity "floor"
    {
        transform position 0 -0.5 0 yaw 0
        collider box half_extents 50 0.5 50
    }
}
)";
}

TEST_CASE("persistent id is stable and string-backed") {
    const PersistentId a = PersistentId::make("movement_test", "player");
    const PersistentId b = PersistentId::from_string("movement_test/player");
    REQUIRE(a == b);
    REQUIRE(a.str() == "movement_test/player");
    REQUIRE(a.hash() != 0);
    REQUIRE(a != PersistentId::make("movement_test", "floor"));
}

TEST_CASE("arena loader populates entities, components and persistent ids") {
    MovementWorld world;
    const ArenaLoadResult result = load_arena_from_string(kArena, "test.arena", world);

    REQUIRE(result.arena_id == "movement_test");
    REQUIRE(result.version == 1);
    REQUIRE(world.is_alive(result.player));

    // PersistentId derived as arena_id/entity_name.
    REQUIRE(world.persistent_id(result.player)->str() == "movement_test/player");

    const Transform* tf = world.transforms().get(result.player);
    REQUIRE(tf != nullptr);
    REQUIRE(tf->position.y == 1.1f);
    REQUIRE(tf->previous_position == tf->position); // initialized

    const PlayerController* pc = world.controllers().get(result.player);
    REQUIRE(pc != nullptr);
    REQUIRE(pc->speed == 4.0f);
    REQUIRE(pc->jump_velocity == 7.0f);
    REQUIRE(pc->gravity == 18.0f);

    const Collider* col = world.colliders().get(result.player);
    REQUIRE(col != nullptr);
    REQUIRE(col->shape == ColliderShape::Capsule);
    REQUIRE(col->radius == 0.4f);

    // The floor entity exists with a box collider.
    const EntityId floor = world.find(PersistentId::make("movement_test", "floor"));
    REQUIRE(world.is_alive(floor));
    REQUIRE(world.colliders().get(floor)->shape == ColliderShape::Box);
}

TEST_CASE("unknown component is rejected with file location") {
    const char* src = R"(arena "x" version 1
{
    entity "p"
    {
        transform position 0 0 0
        mystery foo 1
    }
}
)";
    MovementWorld world;
    REQUIRE_THROWS_MATCHES(
        load_arena_from_string(src, "x.arena", world),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("unknown component") &&
                                        ContainsSubstring("mystery")));
}

TEST_CASE("invalid collider shape is rejected") {
    const char* src = R"(arena "x" version 1
{
    entity "p"
    {
        collider sphere radius 1
    }
}
)";
    MovementWorld world;
    REQUIRE_THROWS_MATCHES(
        load_arena_from_string(src, "x.arena", world),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("collider shape") &&
                                        ContainsSubstring("sphere")));
}

TEST_CASE("unknown field inside a component is rejected") {
    const char* src = R"(arena "x" version 1
{
    entity "p"
    {
        player_controller speed 4 jump_velocity 5 gravity 9 wobble 1
    }
}
)";
    MovementWorld world;
    REQUIRE_THROWS_MATCHES(
        load_arena_from_string(src, "x.arena", world),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("unknown field") &&
                                        ContainsSubstring("wobble")));
}
