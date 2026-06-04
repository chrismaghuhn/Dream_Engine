add_executable(engine_tests
    core/test_smoke.cpp
    core/test_math.cpp
    core/test_sim_clock.cpp
    core/test_engine_config.cpp
    core/test_default_toml.cpp
    core/test_job_system.cpp
    world/test_indexing.cpp
    world/test_pack_vertex_lod.cpp
    world/test_chunk_lod_selection.cpp
    world/test_chunk_lod_mesher.cpp
    world/test_world_config.cpp
    world/test_world_events.cpp
    world/test_origin_rebase.cpp
    world/test_palette.cpp
    world/test_occupancy.cpp
    world/test_chunk_lifecycle.cpp
    world/test_streaming.cpp
    world/test_greedy_mesher.cpp
    world/test_section_render_meta.cpp
    world/test_section_mesh_priority.cpp
    world/test_block_light.cpp
    world/test_chunk_unload_gpu.cpp
    procgen/test_heightmap_worldgen.cpp
    procgen/test_terrain_graph.cpp
    gameplay/test_block_events.cpp
    audio/test_occlusion_grid.cpp
    physics/test_capsule_resolver.cpp
    physics/test_physics_system_jolt.cpp
    physics/test_object_layer_matrix.cpp
    physics/test_collision_remesh_queue.cpp
    physics/test_debris_system.cpp
    persist/test_minimal_save.cpp
    persist/test_region_save.cpp
    persist/test_player_dat_inventory.cpp
    render/test_snapshot_ring.cpp
    render/test_deferred_free_queue.cpp
    render/test_host_memory_alignment.cpp
    render/test_staging_ring.cpp
    render/test_gpu_mesh_pool.cpp
    render/test_streaming_terrain_events.cpp
    render/test_section_occlusion.cpp
)
target_link_libraries(engine_tests PRIVATE engine_core engine_audio Catch2::Catch2WithMain)
target_compile_definitions(engine_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
include(Catch)
catch_discover_tests(engine_tests)

# Movement Foundation tests link ONLY engine_movement_core, proving the core
# logic builds free of Vulkan/GLFW/flecs.
add_executable(movement_tests
    movement/test_entity_world.cpp
    movement/test_text_parser.cpp
    movement/test_arena_loader.cpp
    movement/test_save_service.cpp
    movement/test_collision.cpp
    movement/test_camera_rig.cpp
)
target_link_libraries(movement_tests PRIVATE engine_movement_core Catch2::Catch2WithMain)
target_compile_definitions(movement_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
catch_discover_tests(movement_tests)

# Character (glTF ingest, skeleton validation, animation, combat FSM, hit detection).
# Links only engine_character_core — no Vulkan dependency.
add_executable(character_tests
    character/test_gltf_ingest.cpp
    character/test_skeleton_compat.cpp
    character/test_character_catalog.cpp
    character/test_animation_controller.cpp
    character/test_attack_data.cpp
    character/test_input_buffer.cpp
    character/test_combo_fsm.cpp
    character/test_hit_window.cpp
    character/test_hit_overlap.cpp
)
target_link_libraries(character_tests PRIVATE engine_character_core Catch2::Catch2WithMain)
target_compile_definitions(character_tests PRIVATE
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ENGINE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
catch_discover_tests(character_tests)
