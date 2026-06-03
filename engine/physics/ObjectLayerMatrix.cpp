#include "engine/physics/ObjectLayerMatrix.hpp"

namespace engine {

namespace {

[[nodiscard]] bool layer_index_valid(const ObjectLayer layer) {
    return static_cast<std::uint8_t>(layer) < static_cast<std::uint8_t>(ObjectLayer::Count);
}

} // namespace

bool can_collide(const ObjectLayer a, const ObjectLayer b) {
    if (!layer_index_valid(a) || !layer_index_valid(b)) {
        return false;
    }
    if (a == b) {
        return false;
    }

    switch (a) {
    case ObjectLayer::Static:
        return b == ObjectLayer::Player || b == ObjectLayer::Debris;
    case ObjectLayer::Player:
        return b == ObjectLayer::Static || b == ObjectLayer::Debris || b == ObjectLayer::Sensor;
    case ObjectLayer::Debris:
        return b == ObjectLayer::Static || b == ObjectLayer::Player;
    case ObjectLayer::Sensor:
        return b == ObjectLayer::Player;
    case ObjectLayer::Count:
        break;
    }
    return false;
}

} // namespace engine
