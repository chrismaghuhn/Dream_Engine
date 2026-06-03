set(ENGINE_CHARACTER_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/engine/character/core/cgltf_impl.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/GltfIngest.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/SkeletonValidator.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/CookedCharacterCache.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/AnimationController.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/InputBuffer.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/AttackData.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/CombatController.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/HitDetection.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/CharacterCatalog.cpp
    ${CMAKE_SOURCE_DIR}/engine/character/core/HitReactSystem.cpp
)

add_library(engine_character_core STATIC ${ENGINE_CHARACTER_CORE_SOURCES})
target_include_directories(engine_character_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_character_core PUBLIC glm::glm spdlog::spdlog cgltf)
target_compile_definitions(engine_character_core PUBLIC
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ENGINE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/engine/character/render/CharacterPass.cpp")
    add_library(engine_character_render STATIC
        ${CMAKE_SOURCE_DIR}/engine/character/render/CharacterPass.cpp
    )
    target_include_directories(engine_character_render PUBLIC ${CMAKE_SOURCE_DIR})
    target_compile_definitions(engine_character_render PUBLIC VK_NO_PROTOTYPES)
    target_link_libraries(engine_character_render PUBLIC
        engine_character_core
        engine_render
    )
    add_dependencies(engine_character_render shaders_spv)
endif()
