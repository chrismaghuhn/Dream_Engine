#include "engine/render/ShaderManager.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>

namespace engine {

namespace {

[[nodiscard]] std::filesystem::file_time_type file_mtime(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::last_write_time(path, ec);
}

[[nodiscard]] bool run_glslc(const char* stage, const std::filesystem::path& input, const std::filesystem::path& output) {
    const std::string cmd =
        std::string("glslc -fshader-stage=") + stage + " \"" + input.string() + "\" -o \"" + output.string() + "\"";
    const int result = std::system(cmd.c_str());
    return result == 0;
}

} // namespace

void ShaderManager::init(const std::filesystem::path& source_dir, const std::filesystem::path& output_dir) {
    source_dir_ = source_dir;
    output_dir_ = output_dir;
    std::filesystem::create_directories(output_dir_);
}

void ShaderManager::shutdown() {
    shaders_.clear();
    vert_mtime_.clear();
    frag_mtime_.clear();
    shaders_dirty_ = false;
}

void ShaderManager::register_shader(const std::string name, const ShaderSource& source) {
    shaders_[name] = source;
    vert_mtime_[name] = file_mtime(source.vert);
    frag_mtime_[name] = file_mtime(source.frag);
}

const ShaderManager::ShaderSource* ShaderManager::find(const std::string& name) const {
    const auto it = shaders_.find(name);
    return it == shaders_.end() ? nullptr : &it->second;
}

bool ShaderManager::compile_pair(const ShaderSource& source) {
    if (!std::filesystem::exists(source.vert) || !std::filesystem::exists(source.frag)) {
        SPDLOG_WARN("Shader sources missing: {} / {}", source.vert.string(), source.frag.string());
        return false;
    }

    const bool vert_ok = run_glslc("vert", source.vert, source.vert_spv);
    const bool frag_ok = run_glslc("frag", source.frag, source.frag_spv);
    return vert_ok && frag_ok;
}

void ShaderManager::poll_hot_reload() {
#if defined(NDEBUG)
    return;
#else
    bool any_changed = false;

    for (auto& [name, source] : shaders_) {
        const auto vert_time = file_mtime(source.vert);
        const auto frag_time = file_mtime(source.frag);

        const auto vert_it = vert_mtime_.find(name);
        const auto frag_it = frag_mtime_.find(name);
        const bool vert_changed =
            vert_it == vert_mtime_.end() || vert_time != vert_it->second;
        const bool frag_changed =
            frag_it == frag_mtime_.end() || frag_time != frag_it->second;

        if (!vert_changed && !frag_changed) {
            continue;
        }

        if (compile_pair(source)) {
            vert_mtime_[name] = vert_time;
            frag_mtime_[name] = frag_time;
            any_changed = true;
            SPDLOG_INFO("Hot-reloaded shader '{}'", name);
        } else {
            SPDLOG_WARN("Hot-reload failed for shader '{}'", name);
        }
    }

    if (any_changed) {
        shaders_dirty_ = true;
    }
#endif
}

} // namespace engine
