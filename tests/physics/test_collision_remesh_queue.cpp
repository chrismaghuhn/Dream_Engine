#include <catch2/catch_test_macros.hpp>

#include "engine/physics/CollisionRemeshQueue.hpp"

TEST_CASE("collision remesh queue deduplicates chunk coords", "[physics][collision]") {
    engine::CollisionRemeshQueue queue;
    const engine::ChunkCoord coord{3, -1, 7};

    queue.enqueue(coord);
    queue.enqueue(coord);
    REQUIRE(queue.pending_count() == 1);
    REQUIRE(queue.has_pending());
}

TEST_CASE("collision remesh queue respects budget cap", "[physics][collision]") {
    engine::CollisionRemeshQueue queue;
    for (int i = 0; i < 8; ++i) {
        queue.enqueue(engine::ChunkCoord{i, 0, 0});
    }

    queue.process(0.f);
    REQUIRE(queue.pending_count() == 8);

    queue.process(1000.f);
    REQUIRE_FALSE(queue.has_pending());
    REQUIRE(queue.pending_count() == 0);
}
