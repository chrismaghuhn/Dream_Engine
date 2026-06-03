#include "engine/core/CrashHandlerWin32.hpp"
#include "engine/core/Log.hpp"

#include <spdlog/spdlog.h>

int main() {
    engine::crash_handler_install();
    engine::log_init();
    SPDLOG_INFO("VoxelEngine starting");
    return 0;
}
