# Compile the single engine SDK source with the pinned host compiler, then embed it.
# 使用固定的宿主编译器编译唯一引擎 SDK 源码，再嵌入引擎。
set(dot_script_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/script")
file(MAKE_DIRECTORY "${dot_script_generated}")
add_custom_command(
    OUTPUT "${dot_script_generated}/BuiltinModules.hpp"
    COMMAND "${DOT_TYPESCRIPT_COMPILER}"
        "${PROJECT_SOURCE_DIR}/assets/scripts/sdk/engine.ts"
        "${PROJECT_SOURCE_DIR}/assets/scripts/sdk/host.d.ts"
        --target ES2022 --module ESNext --moduleResolution Bundler --strict
        --declaration --lib ES2022 --outDir "${dot_script_generated}/compiled"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${dot_script_generated}/compiled"
        "-DHOST=${PROJECT_SOURCE_DIR}/assets/scripts/sdk/host.d.ts"
        "-DOUTPUT=${dot_script_generated}/BuiltinModules.hpp"
        -P "${PROJECT_SOURCE_DIR}/cmake/EmbedScriptModules.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/assets/scripts/sdk/engine.ts"
        "${PROJECT_SOURCE_DIR}/assets/scripts/sdk/host.d.ts"
        "${PROJECT_SOURCE_DIR}/cmake/EmbedScriptModules.cmake"
    VERBATIM)
add_custom_target(DotScriptModules DEPENDS "${dot_script_generated}/BuiltinModules.hpp")
add_dependencies(DotEngine DotScriptModules)
target_include_directories(DotEngine PRIVATE "${dot_script_generated}")
