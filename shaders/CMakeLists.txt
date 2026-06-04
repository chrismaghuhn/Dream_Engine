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

function(engine_add_shader target_name stage source_file)
    get_filename_component(source_name "${source_file}" NAME)
    set(output_spv "${SHADER_OUTPUT_DIR}/${source_name}.spv")
    add_custom_command(
        OUTPUT "${output_spv}"
        COMMAND "${GLSLC}" -fshader-stage=${stage} "${source_file}" -o "${output_spv}"
        DEPENDS "${source_file}"
        COMMENT "Compiling ${source_name}"
        VERBATIM
    )
    set_property(GLOBAL APPEND PROPERTY ENGINE_SHADER_SPV_OUTPUTS "${output_spv}")
endfunction()

engine_add_shader(terrain vert "${CMAKE_CURRENT_SOURCE_DIR}/terrain.vert")
engine_add_shader(terrain frag "${CMAKE_CURRENT_SOURCE_DIR}/terrain.frag")
engine_add_shader(sky vert "${CMAKE_CURRENT_SOURCE_DIR}/sky.vert")
engine_add_shader(sky frag "${CMAKE_CURRENT_SOURCE_DIR}/sky.frag")
engine_add_shader(water vert "${CMAKE_CURRENT_SOURCE_DIR}/water.vert")
engine_add_shader(water frag "${CMAKE_CURRENT_SOURCE_DIR}/water.frag")
engine_add_shader(impostor vert "${CMAKE_CURRENT_SOURCE_DIR}/impostor.vert")
engine_add_shader(impostor frag "${CMAKE_CURRENT_SOURCE_DIR}/impostor.frag")
engine_add_shader(debug_lines vert "${CMAKE_CURRENT_SOURCE_DIR}/debug_lines.vert")
engine_add_shader(debug_lines frag "${CMAKE_CURRENT_SOURCE_DIR}/debug_lines.frag")
engine_add_shader(character_skinned vert "${CMAKE_CURRENT_SOURCE_DIR}/character_skinned.vert")
engine_add_shader(character_skinned frag "${CMAKE_CURRENT_SOURCE_DIR}/character_skinned.frag")

get_property(ENGINE_SHADER_SPV_OUTPUTS GLOBAL PROPERTY ENGINE_SHADER_SPV_OUTPUTS)
add_custom_target(shaders_spv DEPENDS ${ENGINE_SHADER_SPV_OUTPUTS})
