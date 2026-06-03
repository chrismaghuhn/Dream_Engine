find_program(GLSLC glslc
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/bin"
    REQUIRED
)

if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK and set VULKAN_SDK.")
endif()

set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

set(TERRAIN_VERT "${CMAKE_CURRENT_SOURCE_DIR}/terrain.vert")
set(TERRAIN_FRAG "${CMAKE_CURRENT_SOURCE_DIR}/terrain.frag")
set(TERRAIN_VERT_SPV "${SHADER_OUTPUT_DIR}/terrain.vert.spv")
set(TERRAIN_FRAG_SPV "${SHADER_OUTPUT_DIR}/terrain.frag.spv")

add_custom_command(
    OUTPUT "${TERRAIN_VERT_SPV}"
    COMMAND "${GLSLC}" -fshader-stage=vert "${TERRAIN_VERT}" -o "${TERRAIN_VERT_SPV}"
    DEPENDS "${TERRAIN_VERT}"
    COMMENT "Compiling terrain.vert"
    VERBATIM
)

add_custom_command(
    OUTPUT "${TERRAIN_FRAG_SPV}"
    COMMAND "${GLSLC}" -fshader-stage=frag "${TERRAIN_FRAG}" -o "${TERRAIN_FRAG_SPV}"
    DEPENDS "${TERRAIN_FRAG}"
    COMMENT "Compiling terrain.frag"
    VERBATIM
)

add_custom_target(shaders_spv DEPENDS "${TERRAIN_VERT_SPV}" "${TERRAIN_FRAG_SPV}")
