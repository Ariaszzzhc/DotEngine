# Embed independently authored WGSL as private engine implementation data.
# 将独立维护的 WGSL 嵌入为引擎私有实现数据。
set(dot_shader_source "${PROJECT_SOURCE_DIR}/src/Engine/Graphics/Shaders")
set(dot_shader_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
file(MAKE_DIRECTORY "${dot_shader_generated}")
add_custom_command(
    OUTPUT "${dot_shader_generated}/BuiltinShaders.hpp"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${dot_shader_source}"
        "-DOUTPUT=${dot_shader_generated}/BuiltinShaders.hpp"
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS "${dot_shader_source}/Surface.wgsl"
        "${dot_shader_source}/Skinning.wgsl"
        "${dot_shader_source}/Coverage.wgsl"
        "${dot_shader_source}/ToneMapping.wgsl"
        "${dot_shader_source}/Environment.wgsl"
        "${dot_shader_source}/SurfaceDirect.wgsl"
        "${dot_shader_source}/SurfaceEnvironment.wgsl"
        "${dot_shader_source}/Sky.wgsl"
        "${dot_shader_source}/ShadowDepth.wgsl"
        "${dot_shader_source}/VisibilityDirect.wgsl"
        "${dot_shader_source}/VisibilityShadows.wgsl"
        "${dot_shader_source}/Geometry.wgsl"
        "${dot_shader_source}/Occlusion.wgsl"
        "${dot_shader_source}/OcclusionFilter.wgsl"
        "${dot_shader_source}/AmbientOcclusion.wgsl"
        "${dot_shader_source}/AmbientUnoccluded.wgsl"
        "${dot_shader_source}/Picking.wgsl"
        "${PROJECT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    VERBATIM)
add_custom_target(DotShaders DEPENDS "${dot_shader_generated}/BuiltinShaders.hpp")
add_dependencies(DotEngine DotShaders)
target_include_directories(DotEngine PRIVATE "${dot_shader_generated}")
