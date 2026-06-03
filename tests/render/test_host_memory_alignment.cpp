#include <catch2/catch_test_macros.hpp>

#include "engine/render/HostMemory.hpp"

using engine::align_mapped_range;

TEST_CASE("host_write_aligns_flush_range_to_non_coherent_atom_size") {
    VkMappedMemoryRange range{
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .offset = 100,
        .size = 32,
    };

    align_mapped_range(range, 64);

    REQUIRE(range.offset == 64);
    REQUIRE(range.size == 128);
}

TEST_CASE("align_mapped_range_expands_partial_atom_tail") {
    VkMappedMemoryRange range{
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .offset = 128,
        .size = 1,
    };

    align_mapped_range(range, 64);

    REQUIRE(range.offset == 128);
    REQUIRE(range.size == 64);
}

TEST_CASE("host_write_skips_flush_for_host_coherent_memory") {
    engine::GpuCaps caps{.non_coherent_atom_size = 64};
    char dst[256]{};
    const char src[4] = {'a', 'b', 'c', 'd'};

    REQUIRE_NOTHROW(engine::host_write(VK_NULL_HANDLE,
                                       dst,
                                       100,
                                       sizeof(src),
                                       src,
                                       VK_NULL_HANDLE,
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       caps));
}
