#pragma once

#include <memory>

namespace spdlog {
class logger;
}

namespace engine {

void log_init();

// Stub until M0-4 loads per-module levels from TOML.
std::shared_ptr<spdlog::logger> module_logger(const char* name);

} // namespace engine
