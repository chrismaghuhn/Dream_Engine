#include "movement/MovementApp.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>
#include <vector>

#ifndef ENGINE_SOURCE_DIR
#define ENGINE_SOURCE_DIR "."
#endif

int main() {
    // Console + file sinks so a hard crash still leaves a readable trail in
    // <repo>/movement_log.txt.
    try {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            std::string(ENGINE_SOURCE_DIR) + "/movement_log.txt", true));
        auto logger = std::make_shared<spdlog::logger>("movement", sinks.begin(), sinks.end());
        spdlog::set_default_logger(logger);
    } catch (const std::exception&) {
        // Fall back to the default logger if file sink creation fails.
    }
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);

    SPDLOG_INFO("movement_test starting");

    engine::movement::MovementApp app;
    if (!app.startup()) {
        SPDLOG_ERROR("movement_test: startup failed");
        spdlog::shutdown();
        app.shutdown();
        return EXIT_FAILURE;
    }
    app.run();
    app.shutdown();
    spdlog::shutdown();
    return EXIT_SUCCESS;
}
