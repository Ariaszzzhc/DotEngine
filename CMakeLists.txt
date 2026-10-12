cmake_minimum_required(VERSION 3.31)

project(DotEngine VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 AND NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "DotEngine supports Windows, macOS, and Linux")
endif()

include(CTest)
option(DOT_WARNINGS_AS_ERRORS "Treat engine warnings as errors" ON)

add_library(DotBuildOptions INTERFACE)
target_compile_features(DotBuildOptions INTERFACE cxx_std_26)
if(MSVC)
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
add_library(DotEngine STATIC)
target_link_libraries(DotEngine PUBLIC DotBuildOptions Threads::Threads)
target_sources(DotEngine PUBLIC
    FILE_SET CXX_MODULES FILES
        src/Engine/Core/Types.cppm
        src/Engine/Core/Result.cppm
        src/Engine/Core/Logging.cppm
        src/Engine/Core/Clock.cppm
        src/Engine/Core/Memory.cppm
        src/Engine/Core/Core.cppm
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
