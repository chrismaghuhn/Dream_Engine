#include <catch2/catch_test_macros.hpp>

#include "engine/movement/core/ComponentStore.hpp"
#include "engine/movement/core/Components.hpp"
#include "engine/movement/core/EntityWorld.hpp"
#include "engine/movement/core/Interpolation.hpp"
#include "engine/movement/core/MovementWorld.hpp"

using namespace engine::movement;

TEST_CASE("entity id packs index and generation") {
    const EntityId id{1234u, 7u};
    REQUIRE(id.index() == 1234u);
    REQUIRE(id.generation() == 7u);
    REQUIRE_FALSE(id.is_null());
    REQUIRE(EntityId::null().is_null());
}

TEST_CASE("entity world create and alive") {
    EntityWorld world;
    const EntityId a = world.create();
    const EntityId b = world.create();
    REQUIRE(world.is_alive(a));
    REQUIRE(world.is_alive(b));
    REQUIRE(a != b);
    REQUIRE(world.alive_count() == 2);
}

TEST_CASE("index is recycled but generation invalidates stale handle") {
    EntityWorld world;
    const EntityId a = world.create();
    const std::uint32_t index = a.index();
    world.destroy(a);

    const EntityId b = world.create();
    REQUIRE(b.index() == index);
    REQUIRE(b.generation() != a.generation());
    REQUIRE(world.is_alive(b));
    REQUIRE_FALSE(world.is_alive(a));
}

TEST_CASE("double destroy is a no-op") {
    EntityWorld world;
    const EntityId a = world.create();
    world.destroy(a);
    world.destroy(a);
    REQUIRE(world.alive_count() == 0);
}

TEST_CASE("component store add/get/has/remove") {
    ComponentStore<Transform> store;
    EntityWorld world;
    const EntityId a = world.create();
    const EntityId b = world.create();

    REQUIRE_FALSE(store.has(a));
    Transform t;
    t.position = glm::vec3(1.f, 2.f, 3.f);
    store.add(a, t);
    REQUIRE(store.has(a));
    REQUIRE(store.get(a) != nullptr);
    REQUIRE(store.get(a)->position == glm::vec3(1.f, 2.f, 3.f));
    REQUIRE_FALSE(store.has(b));
    REQUIRE(store.size() == 1);

    store.remove(a);
    REQUIRE_FALSE(store.has(a));
    REQUIRE(store.get(a) == nullptr);
    REQUIRE(store.size() == 0);
}

TEST_CASE("movement world spawns with persistent id and resolves it") {
    MovementWorld world;
    const PersistentId pid = PersistentId::make("arena", "player");
    const EntityId id = world.spawn(pid);

    REQUIRE(world.is_alive(id));
    REQUIRE(world.find(pid) == id);
    REQUIRE(world.persistent_id(id) != nullptr);
    REQUIRE(world.persistent_id(id)->str() == "arena/player");
}

TEST_CASE("destroying an entity removes all of its components") {
    MovementWorld world;
    const EntityId id = world.spawn(PersistentId::make("arena", "player"));
    world.transforms().add(id, Transform{});
    world.colliders().add(id, Collider{});
    world.controllers().add(id, PlayerController{});
    world.camera_rigs().add(id, CameraRig{});

    REQUIRE(world.transforms().has(id));
    REQUIRE(world.colliders().has(id));
    REQUIRE(world.controllers().has(id));
    REQUIRE(world.camera_rigs().has(id));

    world.destroy(id);

    REQUIRE_FALSE(world.is_alive(id));
    REQUIRE_FALSE(world.transforms().has(id));
    REQUIRE_FALSE(world.colliders().has(id));
    REQUIRE_FALSE(world.controllers().has(id));
    REQUIRE_FALSE(world.camera_rigs().has(id));
    REQUIRE(world.find(PersistentId::make("arena", "player")).is_null());
}

TEST_CASE("transform interpolation lerps position and angle") {
    const glm::vec3 mid = lerp_position(glm::vec3(0.f), glm::vec3(10.f, 0.f, 0.f), 0.5f);
    REQUIRE(mid.x == 5.f);

    // Shortest-path angle interpolation wraps correctly.
    const float a = lerp_angle(0.f, 1.f, 0.5f);
    REQUIRE(a > 0.49f);
    REQUIRE(a < 0.51f);
}
