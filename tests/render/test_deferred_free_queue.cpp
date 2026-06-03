#include <catch2/catch_test_macros.hpp>

#include "engine/render/GpuDeferredFreeQueue.hpp"

#include <array>
#include <vector>

using engine::GpuDeferredFreeQueue;

TEST_CASE("deferred_free_does_not_free_until_last_used_fence_signaled") {
    std::vector<std::uint32_t> freed;
    GpuDeferredFreeQueue queue(2, [&](std::uint32_t slot_id) { freed.push_back(slot_id); });

    std::array<bool, 2> signaled{false, false};
    queue.set_fence_checker([&](std::uint32_t ring_index) { return signaled[ring_index]; });

    queue.enqueue_free(42, 0);
    queue.process_completed();
    REQUIRE(freed.empty());
    REQUIRE(queue.pending_count() == 1);

    signaled[0] = true;
    queue.process_completed();
    REQUIRE(freed.size() == 1);
    REQUIRE(freed[0] == 42);
    REQUIRE(queue.pending_count() == 0);
}

TEST_CASE("deferred_free_waits_for_matching_frame_fence") {
    std::vector<std::uint32_t> freed;
    GpuDeferredFreeQueue queue(2, [&](std::uint32_t slot_id) { freed.push_back(slot_id); });

    std::array<bool, 2> signaled{false, false};
    queue.set_fence_checker([&](std::uint32_t ring_index) { return signaled[ring_index]; });

    queue.enqueue_free(7, 1);
    signaled[0] = true;
    queue.process_completed();
    REQUIRE(freed.empty());

    signaled[1] = true;
    queue.process_completed();
    REQUIRE(freed.size() == 1);
    REQUIRE(freed[0] == 7);
}
