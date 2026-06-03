#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "engine/movement/core/ArenaLoader.hpp"
#include "engine/movement/core/CameraSystem.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/MovementWorld.hpp"
#include "engine/movement/core/SaveService.hpp"

#include <filesystem>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
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
}
)";

EntityId load_world(MovementWorld& world) {
    return load_arena_from_string(kArena, "test.arena", world).player;
}

std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}
} // namespace

TEST_CASE("save/load roundtrip preserves transform, controller runtime and camera") {
    MovementWorld src;
    const EntityId player = load_world(src);

    src.transforms().get(player)->position = glm::vec3(1.2f, 1.0f, 3.4f);
    src.transforms().get(player)->yaw = 1.5f;
    src.controllers().get(player)->velocity = glm::vec3(0.5f, -2.f, 0.25f);
    src.controllers().get(player)->grounded = true;
    src.camera_rigs().get(player)->yaw = 0.75f;
    camera::set_pitch_degrees(*src.camera_rigs().get(player), -12.f);
    src.camera_rigs().get(player)->distance = 4.0f;

    const std::string text = SaveService::serialize(src, "slot_01", "movement_test");

    MovementWorld dst;
    const EntityId dst_player = load_world(dst);
    SaveService::load_from_string(text, "mem", dst, "movement_test");

    const Transform* tf = dst.transforms().get(dst_player);
    REQUIRE_THAT(tf->position.x, WithinAbs(1.2, 1e-5));
    REQUIRE_THAT(tf->position.z, WithinAbs(3.4, 1e-5));
    REQUIRE_THAT(tf->yaw, WithinAbs(1.5, 1e-5));

    const PlayerController* pc = dst.controllers().get(dst_player);
    REQUIRE_THAT(pc->velocity.x, WithinAbs(0.5, 1e-5));
    REQUIRE_THAT(pc->velocity.y, WithinAbs(-2.0, 1e-5));
    REQUIRE(pc->grounded == true);

    const CameraRig* rig = dst.camera_rigs().get(dst_player);
    REQUIRE_THAT(rig->yaw, WithinAbs(0.75, 1e-5));
    REQUIRE_THAT(glm::degrees(rig->pitch), WithinAbs(-12.0, 1e-3));
    REQUIRE_THAT(rig->distance, WithinAbs(4.0, 1e-5));
}

TEST_CASE("loading sets previous_transform = current_transform") {
    MovementWorld src;
    const EntityId player = load_world(src);
    src.transforms().get(player)->position = glm::vec3(7.f, 8.f, 9.f);

    const std::string text = SaveService::serialize(src, "slot", "movement_test");

    MovementWorld dst;
    const EntityId dp = load_world(dst);
    // Dirty the previous to prove load resets it.
    dst.transforms().get(dp)->previous_position = glm::vec3(-100.f);
    SaveService::load_from_string(text, "mem", dst, "movement_test");

    const Transform* tf = dst.transforms().get(dp);
    REQUIRE(tf->previous_position == tf->position);
    REQUIRE_THAT(tf->position.y, WithinAbs(8.0, 1e-5));
}

TEST_CASE("override of unknown persistent id is rejected") {
    const char* save = R"(save "slot" version 1
{
    arena "movement_test"
    override "movement_test/ghost"
    {
        transform position 0 0 0
    }
}
)";
    MovementWorld world;
    load_world(world);
    REQUIRE_THROWS_MATCHES(
        SaveService::load_from_string(save, "s.save", world, "movement_test"),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("unknown PersistentId") &&
                                        ContainsSubstring("ghost")));
}

TEST_CASE("save referencing the wrong arena is rejected") {
    const char* save = R"(save "slot" version 1
{
    arena "other_arena"
    override "other_arena/player"
    {
        transform position 0 0 0
    }
}
)";
    MovementWorld world;
    load_world(world);
    REQUIRE_THROWS_MATCHES(
        SaveService::load_from_string(save, "s.save", world, "movement_test"),
        ParseException,
        Catch::Matchers::MessageMatches(ContainsSubstring("other_arena")));
}

TEST_CASE("save writes and reads back through a file") {
    MovementWorld src;
    const EntityId player = load_world(src);
    src.transforms().get(player)->position = glm::vec3(11.f, 12.f, 13.f);

    const std::filesystem::path path = temp_path("movement_world.save");
    std::filesystem::remove(path);
    SaveService::save(path, src, "slot_01", "movement_test");
    REQUIRE(std::filesystem::exists(path));

    MovementWorld dst;
    const EntityId dp = load_world(dst);
    SaveService::load(path, dst, "movement_test");
    REQUIRE_THAT(dst.transforms().get(dp)->position.y, WithinAbs(12.0, 1e-5));

    std::filesystem::remove(path);
}
