#include <catch2/catch_test_macros.hpp>

#include "engine/physics/ObjectLayerMatrix.hpp"

namespace {

using engine::ObjectLayer;

void require_symmetric(const ObjectLayer a, const ObjectLayer b, const bool expected) {
    REQUIRE(engine::can_collide(a, b) == expected);
    REQUIRE(engine::can_collide(b, a) == expected);
}

} // namespace

TEST_CASE("object layer matrix matches spec section 20", "[physics][layers]") {
    for (const ObjectLayer layer : {
             ObjectLayer::Static,
             ObjectLayer::Player,
             ObjectLayer::Debris,
             ObjectLayer::Sensor,
         }) {
        REQUIRE_FALSE(engine::can_collide(layer, layer));
    }

    require_symmetric(ObjectLayer::Static, ObjectLayer::Player, true);
    require_symmetric(ObjectLayer::Static, ObjectLayer::Debris, true);
    require_symmetric(ObjectLayer::Static, ObjectLayer::Sensor, false);

    require_symmetric(ObjectLayer::Player, ObjectLayer::Debris, true);
    require_symmetric(ObjectLayer::Player, ObjectLayer::Sensor, true);

    require_symmetric(ObjectLayer::Debris, ObjectLayer::Sensor, false);
}

TEST_CASE("object layer matrix rejects invalid layers", "[physics][layers]") {
    REQUIRE_FALSE(engine::can_collide(ObjectLayer::Count, ObjectLayer::Static));
    REQUIRE_FALSE(engine::can_collide(ObjectLayer::Static, ObjectLayer::Count));
}
