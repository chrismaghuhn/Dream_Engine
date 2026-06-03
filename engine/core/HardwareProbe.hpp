#pragma once

#include <cstddef>

namespace engine {

struct CpuHardware {
    int logical_cores = 1;
    int physical_cores = 1;
    size_t ram_bytes = 0;
    bool has_ssd = true;
};

struct HardwareProbe {
    static CpuHardware run_cpu();
};

} // namespace engine
