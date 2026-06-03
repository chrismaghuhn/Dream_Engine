#include "engine/core/Log.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#include <KnownFolders.h>
#endif

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace engine {
namespace {

#ifdef _WIN32
std::filesystem::path local_app_data_path() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        throw std::runtime_error("SHGetKnownFolderPath(FOLDERID_LocalAppData) failed");
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}
#endif

std::filesystem::path default_log_file_path() {
#ifdef _WIN32
    const auto log_dir = local_app_data_path() / "VoxelEngine" / "logs";
    std::filesystem::create_directories(log_dir);
    return log_dir / "engine.log";
#else
    return "engine.log";
#endif
}

} // namespace

void log_init() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    // Defaults match assets/default.toml until EngineConfig (M0-4).
    constexpr std::size_t kMaxFileBytes = 16 * 1024 * 1024;
    constexpr std::size_t kMaxFiles = 3;

    spdlog::init_thread_pool(8192, 1);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        default_log_file_path().string(), kMaxFileBytes, kMaxFiles));

#if !defined(NDEBUG)
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif

    auto logger = std::make_shared<spdlog::async_logger>(
        "engine", sinks.begin(), sinks.end(), spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
}

std::shared_ptr<spdlog::logger> module_logger(const char* name) {
    auto existing = spdlog::get(name);
    if (existing) {
        return existing;
    }
    auto fallback = spdlog::default_logger();
    if (!fallback) {
        log_init();
        fallback = spdlog::default_logger();
    }
    return fallback ? fallback : spdlog::stdout_color_mt(name);
}

} // namespace engine
