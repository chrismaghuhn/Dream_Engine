#pragma once

#include <cstdint>

namespace engine {

enum class ObjectLayer : std::uint8_t {
    Static = 0,
    Player = 1,
    Debris = 2,
    Sensor = 3,
    Count = 4,
};

[[nodiscard]] bool can_collide(ObjectLayer a, ObjectLayer b);

} // namespace engine
