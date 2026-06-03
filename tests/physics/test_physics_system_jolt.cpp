#include <catch2/catch_test_macros.hpp>

#include "engine/physics/PhysicsSystem.hpp"

TEST_CASE("PhysicsSystem init with Jolt", "[physics][jolt]") {
#if !defined(ENGINE_HAS_JOLT)
    SKIP("Jolt Physics not linked (ENGINE_HAS_JOLT=0)");
#else
    engine::PhysicsSystem physics;
    REQUIRE(physics.init());
    REQUIRE(physics.is_active());
    physics.shutdown();
    REQUIRE_FALSE(physics.is_active());
#endif
}
