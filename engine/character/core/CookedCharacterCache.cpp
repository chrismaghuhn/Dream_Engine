#include "engine/character/core/CookedCharacterCache.hpp"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>

namespace engine::character {

namespace {

// FNV-1a 64-bit hash of a string — used to derive cache file names.
std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : text) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ---- Simple binary serialization helpers ----

void write_u32(std::ofstream& out, std::uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void write_u64(std::ofstream& out, std::uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void write_f32(std::ofstream& out, float v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void write_i32(std::ofstream& out, int v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void write_str(std::ofstream& out, const std::string& s) {
    write_u32(out, static_cast<std::uint32_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}
template <typename T>
void write_vec(std::ofstream& out, const std::vector<T>& v) {
    write_u32(out, static_cast<std::uint32_t>(v.size()));
    if (!v.empty()) {
        out.write(reinterpret_cast<const char*>(v.data()),
                  static_cast<std::streamsize>(v.size() * sizeof(T)));
    }
}

std::uint32_t read_u32(std::ifstream& in) {
    std::uint32_t v = 0;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}
std::uint64_t read_u64(std::ifstream& in) {
    std::uint64_t v = 0;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}
float read_f32(std::ifstream& in) {
    float v = 0.f;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}
int read_i32(std::ifstream& in) {
    int v = 0;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return v;
}
std::string read_str(std::ifstream& in) {
    const auto len = read_u32(in);
    std::string s(len, '\0');
    in.read(s.data(), static_cast<std::streamsize>(len));
    return s;
}
template <typename T>
std::vector<T> read_vec(std::ifstream& in) {
    const auto count = read_u32(in);
    std::vector<T> v(count);
    if (count > 0) {
        in.read(reinterpret_cast<char*>(v.data()),
                static_cast<std::streamsize>(count * sizeof(T)));
    }
    return v;
}

// Magic bytes and version for the .charbin format.
constexpr std::uint32_t kMagic   = 0x52414843u; // "CHAR" little-endian
constexpr std::uint32_t kVersion = 1u;

} // namespace

// ---------------------------------------------------------------------------

std::filesystem::path CookedCharacterCache::cache_path_for(const std::string& source_glb) {
    const std::uint64_t hash = fnv1a(source_glb);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.charbin",
                  static_cast<unsigned long long>(hash));
#ifndef ENGINE_BINARY_DIR
#define ENGINE_BINARY_DIR "."
#endif
    return std::filesystem::path(ENGINE_BINARY_DIR) / "character_cache" / name;
}

CharacterAsset CookedCharacterCache::load_or_cook(const std::string& source_glb,
                                                   std::function<CharacterAsset()> ingest_fn) {
    const std::filesystem::path cache_path = cache_path_for(source_glb);

    std::error_code ec;
    const bool cache_exists = std::filesystem::exists(cache_path, ec);
    bool use_cache = false;
    if (cache_exists) {
        const auto source_time = std::filesystem::last_write_time(source_glb, ec);
        if (!ec) {
            const auto cache_time = std::filesystem::last_write_time(cache_path, ec);
            if (!ec && cache_time >= source_time) {
                use_cache = true;
            }
        }
    }

    if (use_cache) {
        try {
            CharacterAsset asset = read(cache_path);
            SPDLOG_DEBUG("CookedCharacterCache: loaded from cache {}", cache_path.string());
            return asset;
        } catch (const std::exception& ex) {
            SPDLOG_WARN("CookedCharacterCache: cache read failed ({}); re-cooking", ex.what());
        }
    }

    SPDLOG_INFO("CookedCharacterCache: cooking {}", source_glb);
    CharacterAsset asset = ingest_fn();
    try {
        write(cache_path, asset);
        SPDLOG_INFO("CookedCharacterCache: written to {}", cache_path.string());
    } catch (const std::exception& ex) {
        SPDLOG_WARN("CookedCharacterCache: cache write failed: {}", ex.what());
    }
    return asset;
}

void CookedCharacterCache::write(const std::filesystem::path& cache_path,
                                 const CharacterAsset& asset) {
    std::filesystem::create_directories(cache_path.parent_path());

    std::ofstream out(cache_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("CookedCharacterCache: cannot open for write: " +
                                 cache_path.string());
    }

    write_u32(out, kMagic);
    write_u32(out, kVersion);
    write_str(out, asset.source_path);
    // node_transform (16 floats, column-major)
    out.write(reinterpret_cast<const char*>(&asset.node_transform[0][0]),
              sizeof(glm::mat4));

    // Mesh
    const SkinnedMeshData& m = asset.mesh;
    write_vec(out, m.positions);
    write_vec(out, m.normals);
    write_vec(out, m.uvs);
    write_vec(out, m.joint_indices);
    write_vec(out, m.joint_weights);
    write_vec(out, m.indices);
    write_u32(out, static_cast<std::uint32_t>(m.bones.size()));
    for (const BoneInfo& b : m.bones) {
        write_str(out, b.name);
        write_i32(out, b.parent);
    }
    write_vec(out, m.inverse_bind_matrices);
    write_vec(out, m.base_color_rgba);
    write_i32(out, m.base_color_width);
    write_i32(out, m.base_color_height);

    // Clips
    write_u32(out, static_cast<std::uint32_t>(asset.clips.size()));
    for (const AnimClip& clip : asset.clips) {
        write_str(out, clip.name);
        write_f32(out, clip.duration_seconds);
        write_u32(out, static_cast<std::uint32_t>(clip.channels.size()));
        for (const AnimChannel& ch : clip.channels) {
            write_str(out, ch.target_joint);
            write_vec(out, ch.translation_times);
            write_vec(out, ch.translations);
            write_vec(out, ch.rotation_times);
            write_vec(out, ch.rotations);
            write_vec(out, ch.scale_times);
            write_vec(out, ch.scales);
        }
    }
}

CharacterAsset CookedCharacterCache::read(const std::filesystem::path& cache_path) {
    std::ifstream in(cache_path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("CookedCharacterCache: cannot open: " +
                                 cache_path.string());
    }

    if (read_u32(in) != kMagic) {
        throw std::runtime_error("CookedCharacterCache: bad magic in " +
                                 cache_path.string());
    }
    if (read_u32(in) != kVersion) {
        throw std::runtime_error("CookedCharacterCache: version mismatch in " +
                                 cache_path.string());
    }

    CharacterAsset asset;
    asset.source_path = read_str(in);
    in.read(reinterpret_cast<char*>(&asset.node_transform[0][0]), sizeof(glm::mat4));

    SkinnedMeshData& m = asset.mesh;
    m.positions              = read_vec<glm::vec3>(in);
    m.normals                = read_vec<glm::vec3>(in);
    m.uvs                    = read_vec<glm::vec2>(in);
    m.joint_indices          = read_vec<glm::uvec4>(in);
    m.joint_weights          = read_vec<glm::vec4>(in);
    m.indices                = read_vec<std::uint32_t>(in);
    const auto bone_count    = read_u32(in);
    m.bones.resize(bone_count);
    for (BoneInfo& b : m.bones) {
        b.name   = read_str(in);
        b.parent = read_i32(in);
    }
    m.inverse_bind_matrices  = read_vec<glm::mat4>(in);
    m.base_color_rgba        = read_vec<std::uint8_t>(in);
    m.base_color_width       = read_i32(in);
    m.base_color_height      = read_i32(in);

    const auto clip_count = read_u32(in);
    asset.clips.resize(clip_count);
    for (AnimClip& clip : asset.clips) {
        clip.name             = read_str(in);
        clip.duration_seconds = read_f32(in);
        const auto ch_count   = read_u32(in);
        clip.channels.resize(ch_count);
        for (AnimChannel& ch : clip.channels) {
            ch.target_joint       = read_str(in);
            ch.translation_times  = read_vec<float>(in);
            ch.translations       = read_vec<glm::vec3>(in);
            ch.rotation_times     = read_vec<float>(in);
            ch.rotations          = read_vec<glm::quat>(in);
            ch.scale_times        = read_vec<float>(in);
            ch.scales             = read_vec<glm::vec3>(in);
        }
    }

    return asset;
}

} // namespace engine::character
