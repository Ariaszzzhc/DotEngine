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
find_package(Jolt CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h" REQUIRED)
add_library(DotAudioBackend STATIC src/Engine/Audio/Backend.c)
target_compile_features(DotAudioBackend PRIVATE c_std_11)
target_include_directories(DotAudioBackend SYSTEM PRIVATE "${MINIAUDIO_INCLUDE_DIRS}")
target_link_libraries(DotAudioBackend PUBLIC Threads::Threads)
if(APPLE)
    target_compile_definitions(DotAudioBackend PUBLIC MA_NO_RUNTIME_LINKING)
    find_library(DOT_CORE_AUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(DOT_AUDIO_TOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(DOT_CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(DotAudioBackend PUBLIC
        "${DOT_CORE_AUDIO_FRAMEWORK}" "${DOT_AUDIO_TOOLBOX_FRAMEWORK}" "${DOT_CORE_FOUNDATION_FRAMEWORK}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(DotAudioBackend PUBLIC ${CMAKE_DL_LIBS} m)
endif()
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
    src/Engine/Physics/World.cpp
    src/Engine/Physics/Body.cpp
    src/Engine/Physics/SceneSimulation.cpp
    src/Engine/Audio/Clip.cpp
)
target_link_libraries(DotEngine PUBLIC DotBuildOptions Threads::Threads)
target_link_libraries(DotEngine PRIVATE SDL3::SDL3 glm::glm dawn::webgpu_dawn)
target_link_libraries(DotEngine PRIVATE nlohmann_json::nlohmann_json tinyobjloader::tinyobjloader)
target_link_libraries(DotEngine PRIVATE EnTT::EnTT)
target_link_libraries(DotEngine PRIVATE Jolt::Jolt)
target_link_libraries(DotEngine PRIVATE DotAudioBackend)
target_include_directories(DotEngine SYSTEM PRIVATE "${MINIAUDIO_INCLUDE_DIRS}")
# Module producers and consumers must agree on compiler threading mode.
# 模块生产者与消费者必须使用一致的编译器线程模式。
target_compile_options(DotEngine PUBLIC "$<TARGET_PROPERTY:Jolt::Jolt,INTERFACE_COMPILE_OPTIONS>")
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
        src/Engine/Physics/World.cppm
        src/Engine/Physics/Body.cppm
        src/Engine/Physics/SceneSimulation.cppm
        src/Engine/Audio/Clip.cppm
)

add_executable(DotRuntime src/Runtime/main.cpp src/Runtime/PhysicsAcceptance.cpp src/Runtime/AudioAcceptance.cpp)
target_link_libraries(DotRuntime PRIVATE DotEngine)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json" SYMBOLIC COPY_ON_ERROR)
endif()
