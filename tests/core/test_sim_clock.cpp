#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/core/SimClock.hpp"

using Catch::Matchers::WithinAbs;

TEST_CASE("sim clock caps accumulator spiral of death") {
    engine::SimClock clock;
    clock.advance(100.0);

    REQUIRE_THAT(clock.accumulator(), WithinAbs(engine::SimClock::max_accumulator, 1e-9));

    std::uint32_t ticks = 0;
    ticks += clock.step([] {});
    ticks += clock.step([] {});

    REQUIRE(ticks == 4);
    REQUIRE_THAT(clock.accumulator(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("sim clock alpha reflects partial tick") {
    engine::SimClock clock;
    clock.advance(engine::SimClock::fixed_dt * 0.5);

    REQUIRE(clock.step([] {}) == 0);
    REQUIRE_THAT(clock.alpha(), WithinAbs(0.5, 1e-9));
}
