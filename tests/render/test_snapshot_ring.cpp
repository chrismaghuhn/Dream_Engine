#include <catch2/catch_test_macros.hpp>

#include "engine/render/SnapshotRing.hpp"

using engine::SnapshotRing;

TEST_CASE("snapshot ring picks signaled slot") {
    SnapshotRing ring(2);
    REQUIRE(ring.pick_write_slot() == 0);
    ring.mark_submitted(0);
    ring.mark_gpu_complete(0);
    REQUIRE(ring.pick_write_slot() == 0);
}
