#pragma once

#include "engine/core/JobSystem.hpp"
#include "engine/gameplay/Inventory.hpp"
#include "engine/persist/MinimalSaveBackend.hpp"
#include "engine/persist/RegionFileSaveBackend.hpp"
#include "engine/persist/WorldMeta.hpp"
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
    InventorySnapshot inventory{};
    JobSystem* jobs = nullptr;
    bool prefer_region_backend = true;
};

class SaveService {
public:
    [[nodiscard]] static std::filesystem::path world_dir(const SaveWorldRequest& request) {
        return save_world_dir(request.saves_root, request.world_name);
    }

    [[nodiscard]] static bool save_world(const SaveWorldRequest& request, ChunkStore& store) {
        const std::filesystem::path dir = world_dir(request);
        if (should_use_region_backend(dir, request)) {
            return region_save_world(
                dir,
                request.world_config,
                request.player_position,
                request.inventory,
                store,
                request.jobs);
        }
        return minimal_save_world(
            dir,
            request.world_config,
            request.player_position,
            request.inventory,
            store);
    }

    [[nodiscard]] static bool load_world(
        const SaveWorldRequest& request,
        WorldPosition& out_player_position,
        InventorySnapshot& out_inventory,
        ChunkStore& store) {
        const std::filesystem::path dir = world_dir(request);

        std::error_code ec;
        const bool has_minimal = std::filesystem::exists(dir / "minimal", ec);
        WorldMeta meta{};
        const bool has_meta = read_world_meta(dir, meta);
        if (has_minimal && (!has_meta || !meta.minimal_migrated)) {
            (void)migrate_minimal_to_region(dir, request.jobs);
        }

        if (should_use_region_backend(dir, request)) {
            return region_load_world(
                dir,
                request.world_config,
                out_player_position,
                out_inventory,
                store);
        }
        return minimal_load_world(
            dir,
            request.world_config,
            out_player_position,
            out_inventory,
            store);
    }

    [[nodiscard]] static bool world_save_exists(const SaveWorldRequest& request) {
        return world_meta_exists(world_dir(request));
    }

private:
    [[nodiscard]] static bool should_use_region_backend(
        const std::filesystem::path& world_dir,
        const SaveWorldRequest& request) {
        if (!request.prefer_region_backend) {
            return false;
        }

        std::error_code ec;
        if (std::filesystem::exists(world_dir / "regions", ec)) {
            return true;
        }

        WorldMeta meta{};
        if (read_world_meta(world_dir, meta)) {
            return !uses_minimal_save_backend(meta);
        }

        return true;
    }
};

} // namespace engine
