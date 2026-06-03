#include <catch2/catch_test_macros.hpp>

#include "engine/character/core/InputBuffer.hpp"

using namespace engine::character;
using Kind = BufferedInput::Kind;

TEST_CASE("empty buffer returns None", "[input_buffer]") {
    InputBuffer buf;
    REQUIRE(buf.peek() == Kind::None);
}

TEST_CASE("pushed input is visible via peek", "[input_buffer]") {
    InputBuffer buf;
    buf.push(Kind::Light);
    REQUIRE(buf.peek() == Kind::Light);
}

TEST_CASE("consume removes oldest entry", "[input_buffer]") {
    InputBuffer buf;
    buf.push(Kind::Light);
    buf.push(Kind::Heavy);
    buf.consume();
    REQUIRE(buf.peek() == Kind::Heavy);
}

TEST_CASE("tick decrements TTL; expired entries removed", "[input_buffer]") {
    InputBuffer buf;
    buf.push(Kind::Kick, 2);
    buf.tick();
    REQUIRE(buf.peek() == Kind::Kick);
    buf.tick();
    REQUIRE(buf.peek() == Kind::None);
}

TEST_CASE("clear empties buffer", "[input_buffer]") {
    InputBuffer buf;
    buf.push(Kind::Light);
    buf.push(Kind::Heavy);
    buf.clear();
    REQUIRE(buf.peek() == Kind::None);
}

TEST_CASE("buffer capacity drops oldest entry when full", "[input_buffer]") {
    InputBuffer buf;
    for (int i = 0; i < InputBuffer::kCapacity + 2; ++i) {
        buf.push(Kind::Light, InputBuffer::kDefaultTTL);
    }
    REQUIRE(buf.peek() == Kind::Light);
}
