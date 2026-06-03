#pragma once
#include <glm/glm.hpp>
namespace engine {
inline int floor_div(int a, int b) {
    int q = a / b, r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}
inline int positive_mod(int a, int b) {
    int r = a % b;
    return r < 0 ? r + b : r;
}
using ChunkCoord = glm::ivec3;
inline ChunkCoord block_to_chunk(int wx, int wy, int wz) {
    return { floor_div(wx, 32), floor_div(wy, 32), floor_div(wz, 32) };
}
inline glm::ivec3 block_local_in_chunk(int wx, int wy, int wz) {
    return {
        positive_mod(wx, 32), positive_mod(wy, 32), positive_mod(wz, 32)
    };
}
}
