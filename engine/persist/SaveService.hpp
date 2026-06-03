#pragma once

#include "engine/persist/MinimalSaveBackend.hpp"
#include "engine/world/ChunkStore.hpp"
#include "engine/world/WorldConfig.hpp"
#include "engine/world/WorldPosition.hpp"

#include <filesystem>
#include <string>

namespace engine {

struct SaveWorldRequest {
    std::filesystem::path saves_root;
    std::string world_name = "default";
    WorldConfig world_config{};
    WorldPosition player_position{};
};

class SaveService {
public:
    [[nodiscard]] static std::filesystem::path world_dir(const SaveWorldRequest& request) {
        return minimal_save_world_dir(request.saves_root, request.world_name);
    }

    [[nodiscard]] static bool save_world(const SaveWorldRequest& request, ChunkStore& store) {
        return minimal_save_world(
            world_dir(request),
            request.world_config,
            request.player_position,
            store);
    }

    [[nodiscard]] static bool load_world(
        const SaveWorldRequest& request,
        WorldPosition& out_player_position,
        ChunkStore& store) {
        return minimal_load_world(
            world_dir(request),
            request.world_config,
            out_player_position,
            store);
    }

    [[nodiscard]] static bool world_save_exists(const SaveWorldRequest& request) {
        std::error_code ec;
        return std::filesystem::exists(world_dir(request) / "world.meta", ec);
    }
};

} // namespace engine
