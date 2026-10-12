cmake_minimum_required(VERSION 3.31)

project(DotEngine VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 AND NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "DotEngine supports Windows, macOS, and Linux")
endif()

include(CTest)
option(DOT_WARNINGS_AS_ERRORS "Treat engine warnings as errors" ON)

add_library(DotBuildOptions INTERFACE)
target_compile_features(DotBuildOptions INTERFACE cxx_std_26)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(DotBuildOptions INTERFACE /W4 /permissive- /utf-8)
    if(DOT_WARNINGS_AS_ERRORS)
        target_compile_options(DotBuildOptions INTERFACE /WX)
    endif()
else()
    target_compile_options(DotBuildOptions INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(DOT_WARNINGS_AS_ERRORS)
        target_compile_options(DotBuildOptions INTERFACE -Werror)
    endif()
endif()
find_package(Threads REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
add_library(DotEngine STATIC)
target_compile_definitions(DotEngine PRIVATE ENTT_USE_ATOMIC)
target_sources(DotEngine PRIVATE
    src/Engine/Core/Clock.cpp
    src/Engine/Core/Files.cpp
    src/Engine/Core/Logging.cpp
    src/Engine/Core/Memory.cpp
    src/Engine/Core/Result.cpp
    src/Engine/Core/Text.cpp
    src/Engine/Core/Application.cpp
    src/Engine/Platform/System.cpp
    src/Engine/Platform/Window.cpp
    src/Engine/Platform/Events.cpp
    src/Engine/Platform/Input.cpp
    src/Engine/Math/Math.cpp
    src/Engine/Graphics/Renderer.cpp
    src/Engine/Graphics/SceneRenderer.cpp
    src/Engine/Graphics/Geometry.cpp
    src/Engine/Assets/Mesh.cpp
    src/Engine/World/Scene.cpp
    src/Engine/World/SceneFiles.cpp
)
target_link_libraries(DotEngine PUBLIC DotBuildOptions Threads::Threads)
target_link_libraries(DotEngine PRIVATE SDL3::SDL3 glm::glm dawn::webgpu_dawn)
target_link_libraries(DotEngine PRIVATE nlohmann_json::nlohmann_json tinyobjloader::tinyobjloader)
target_link_libraries(DotEngine PRIVATE EnTT::EnTT)
target_sources(DotEngine PUBLIC
    FILE_SET CXX_MODULES FILES
        src/Engine/Core/Types.cppm
        src/Engine/Core/Text.cppm
        src/Engine/Core/Result.cppm
        src/Engine/Core/Logging.cppm
        src/Engine/Core/Clock.cppm
        src/Engine/Core/Memory.cppm
        src/Engine/Core/Events.cppm
        src/Engine/Core/Files.cppm
        src/Engine/Core/Core.cppm
        src/Engine/Platform/System.cppm
        src/Engine/Platform/Window.cppm
        src/Engine/Platform/Events.cppm
        src/Engine/Platform/Input.cppm
        src/Engine/Core/Application.cppm
        src/Engine/Math/Math.cppm
        src/Engine/Graphics/Renderer.cppm
        src/Engine/Graphics/SceneRenderer.cppm
        src/Engine/Graphics/Geometry.cppm
        src/Engine/Assets/Mesh.cppm
        src/Engine/World/Scene.cppm
        src/Engine/World/SceneFiles.cppm
)

add_executable(DotRuntime src/Runtime/main.cpp)
target_link_libraries(DotRuntime PRIVATE DotEngine)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json" SYMBOLIC COPY_ON_ERROR)
endif()
