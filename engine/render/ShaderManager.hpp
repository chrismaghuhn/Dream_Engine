#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

/// Tracks GLSL sources and compiles SPIR-V (Debug: mtime hot-reload).
class ShaderManager {
public:
    struct ShaderSource {
        std::filesystem::path vert;
        std::filesystem::path frag;
        std::filesystem::path vert_spv;
        std::filesystem::path frag_spv;
    };

    void init(const std::filesystem::path& source_dir, const std::filesystem::path& output_dir);
    void shutdown();

    void register_shader(std::string name, const ShaderSource& source);
    [[nodiscard]] const ShaderSource* find(const std::string& name) const;

    /// Debug: recompile when source mtimes change. No-op in Release.
    void poll_hot_reload();

    [[nodiscard]] bool shaders_dirty() const { return shaders_dirty_; }
    void clear_dirty() { shaders_dirty_ = false; }

private:
    bool compile_pair(const ShaderSource& source);

    std::filesystem::path source_dir_;
    std::filesystem::path output_dir_;
    std::unordered_map<std::string, ShaderSource> shaders_;
    std::unordered_map<std::string, std::filesystem::file_time_type> vert_mtime_;
    std::unordered_map<std::string, std::filesystem::file_time_type> frag_mtime_;
    bool shaders_dirty_ = false;
};

} // namespace engine
