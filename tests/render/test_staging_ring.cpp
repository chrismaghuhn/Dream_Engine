#include <catch2/catch_test_macros.hpp>

#include "engine/render/StagingRing.hpp"

using engine::StagingRing;
using engine::StagingUpload;

TEST_CASE("staging_ring_slot_size_splits_chunk_mesh_budget") {
    StagingRing ring(2, 2000);
    REQUIRE(ring.slot_size() == 1000);
}

TEST_CASE("staging_ring_overflow_keeps_upload_pending") {
    StagingRing ring(2, 2000);
    const std::size_t staged =
        ring.try_enqueue(0, StagingUpload{.id = 1, .size = 1500});

    REQUIRE(staged == 1000);
    REQUIRE(ring.bytes_used(0) == 1000);
    REQUIRE(ring.pending_upload_count() == 1);
    REQUIRE(ring.pending_uploads().front().id == 1);
    REQUIRE(ring.pending_uploads().front().size == 500);
}

TEST_CASE("staging_ring_does_not_stomp_full_slot") {
    StagingRing ring(1, 1000);
    ring.try_enqueue(0, StagingUpload{.id = 1, .size = 1000});
    ring.try_enqueue(0, StagingUpload{.id = 2, .size = 256});

    REQUIRE(ring.bytes_used(0) == 1000);
    REQUIRE(ring.pending_upload_count() == 1);
    REQUIRE(ring.pending_uploads().back().size == 256);
}
