#include <catch2/catch_test_macros.hpp>

#include "engine/render/GpuDeferredFreeQueue.hpp"
#include "engine/render/GpuMeshPool.hpp"

using engine::GpuDeferredFreeQueue;
using engine::GpuMeshPool;
using engine::MeshBucket;
using engine::pick_mesh_bucket;

TEST_CASE("pick_mesh_bucket selects smallest fitting bucket") {
    REQUIRE(pick_mesh_bucket(100, 100) == MeshBucket::B4K);
    REQUIRE(pick_mesh_bucket(4096, 4096) == MeshBucket::B4K);
    REQUIRE(pick_mesh_bucket(4097, 100) == MeshBucket::B16K);
    REQUIRE(pick_mesh_bucket(100, 70000) == MeshBucket::B256K);
}

TEST_CASE("gpu_mesh_pool rejects allocation when budget is zero") {
    GpuMeshPool pool;
    pool.init(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, nullptr);
    REQUIRE(pool.allocate(128, 128) == 0);
    pool.set_bytes_budget(1024 * 1024);
    REQUIRE(pool.allocate(128, 128) == 0);
}

TEST_CASE("gpu_mesh_pool_regrow_enqueues_deferred_free") {
    std::uint32_t freed_slot = 0;
    bool freed = false;

    GpuDeferredFreeQueue deferred_free(2, [&](const std::uint32_t slot_id) {
        freed_slot = slot_id;
        freed = true;
    });
    deferred_free.set_fence_checker([](const std::uint32_t) { return true; });

    GpuMeshPool pool;
    pool.init(VK_NULL_HANDLE, VK_NULL_HANDLE, 1024 * 1024, &deferred_free);

    const std::uint32_t old_slot = 42;
    const std::uint32_t new_slot = pool.regrow(old_slot, 512, 512, 7);
    REQUIRE(new_slot == 0);
    REQUIRE(deferred_free.pending_count() == 1);

    deferred_free.process_completed();
    REQUIRE(freed);
    REQUIRE(freed_slot == old_slot);
}
