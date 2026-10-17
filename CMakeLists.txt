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
find_package(imgui CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_file(DOT_EDITOR_FONT NAMES EditorSans.ttf
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/dot-editor-font"
    NO_DEFAULT_PATH REQUIRED)
find_file(DOT_EDITOR_FONT_LICENSE NAMES copyright
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/dot-editor-font"
    NO_DEFAULT_PATH REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(qjs CONFIG REQUIRED)
set(DOT_TYPESCRIPT_TOOL_TRIPLET "${VCPKG_TARGET_TRIPLET}")
if(VCPKG_HOST_TRIPLET)
    set(DOT_TYPESCRIPT_TOOL_TRIPLET "${VCPKG_HOST_TRIPLET}")
endif()
find_program(DOT_TYPESCRIPT_COMPILER NAMES tsc
    PATHS "${VCPKG_INSTALLED_DIR}/${DOT_TYPESCRIPT_TOOL_TRIPLET}/tools/typescript-native"
    NO_DEFAULT_PATH REQUIRED)
get_filename_component(DOT_TYPESCRIPT_TOOL_DIRECTORY "${DOT_TYPESCRIPT_COMPILER}" DIRECTORY)
find_file(DOT_TYPESCRIPT_LICENSE NAMES copyright
    PATHS "${VCPKG_INSTALLED_DIR}/${DOT_TYPESCRIPT_TOOL_TRIPLET}/share/vcpkg-tool-typescript"
    NO_DEFAULT_PATH REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h" REQUIRED)
add_library(DotAudioBackend STATIC src/Engine/Audio/Backend.c)
target_compile_features(DotAudioBackend PRIVATE c_std_11)
target_compile_definitions(DotAudioBackend PUBLIC MA_NO_RESOURCE_MANAGER)
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
add_library(DotImageBackend STATIC src/Engine/Assets/ImageBackend.c)
target_compile_features(DotImageBackend PRIVATE c_std_11)
target_include_directories(DotImageBackend SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
add_library(DotModelBackend STATIC src/Engine/Assets/ModelBackend.c)
target_compile_features(DotModelBackend PRIVATE c_std_11)
target_include_directories(DotModelBackend SYSTEM PRIVATE "${CGLTF_INCLUDE_DIR}")
add_library(DotEngine STATIC)
target_include_directories(DotEngine SYSTEM PRIVATE "${CGLTF_INCLUDE_DIR}")
target_compile_definitions(DotEngine PRIVATE ENTT_USE_ATOMIC)
target_sources(DotEngine PRIVATE
    src/Engine/Graphics/Text.cpp
    src/Engine/Graphics/TextRenderer.cpp
    src/Engine/Script/Hud.cpp
    src/Engine/Script/GameClock.cpp
    src/Engine/Animation/Skin.cpp
    src/Engine/Animation/SkinFiles.cpp
    src/Engine/Animation/SceneSkin.cpp
    src/Engine/Animation/Skeleton.cpp
    src/Engine/Animation/Edit.cpp
    src/Engine/Animation/KeyGroup.cpp
    src/Engine/Assets/SceneSkins.cpp
    src/Engine/Animation/SkinSource.cpp
    src/Engine/Graphics/Skinning.cpp
    src/Engine/Animation/Clip.cpp
    src/Engine/Animation/Source.cpp
    src/Engine/Animation/ScenePlayers.cpp
    src/Engine/Animation/Files.cpp
    src/Engine/Animation/SceneAnimator.cpp
    src/Engine/Core/Clock.cpp
    src/Engine/Core/Files.cpp
    src/Engine/Core/Paths.cpp
    src/Engine/Core/StagedDirectory.cpp
    src/Engine/Core/Control.cpp
    src/Engine/Project/Project.cpp
    src/Engine/Project/GameSession.cpp
    src/Engine/Project/ScenePreparation.cpp
    src/Engine/Project/PlayerControl.cpp
    src/Engine/Game/State.cpp
    src/Engine/Game/Saves.cpp
    src/Engine/Script/SceneFlow.cpp
    src/Engine/Script/GameData.cpp
    src/Engine/Project/GamePackage.cpp
    src/Engine/Project/GameExport.cpp
    src/Engine/Project/Templates.cpp
    src/Engine/Editor/Document.cpp
    src/Engine/Editor/HierarchyTree.cpp
    src/Engine/Editor/Resources.cpp
    src/Engine/Editor/MaterialDocument.cpp
    src/Engine/Editor/AssetPreview.cpp
    src/Engine/Editor/PreviewWorker.cpp
    src/Engine/Editor/Play.cpp
    src/Engine/Platform/AgentInput.cpp
    src/Engine/Editor/ViewportCapture.cpp
    src/Engine/Editor/AnimationPreview.cpp
    src/Engine/Editor/AnimationDocument.cpp
    src/Engine/Editor/AnimationClipboard.cpp
    src/Engine/Editor/AnimationKeyMove.cpp
    src/Engine/Editor/SkeletonView.cpp
    src/Engine/Editor/Workspace.cpp
    src/Engine/Editor/Home.cpp
    src/Engine/Core/Logging.cpp
    src/Engine/Core/Memory.cpp
    src/Engine/Core/Process.cpp
    src/Engine/Core/Result.cpp
    src/Engine/Core/Text.cpp
    src/Engine/Core/Application.cpp
    src/Engine/Core/ApplicationProfile.cpp
    src/Engine/Platform/System.cpp
    src/Engine/Platform/UserData.cpp
    src/Engine/Platform/Window.cpp
    src/Engine/Platform/Events.cpp
    src/Engine/Platform/Dialog.cpp
    src/Engine/Platform/Input.cpp
    src/Engine/Math/Math.cpp
    src/Engine/Graphics/Renderer.cpp
    src/Engine/Graphics/GuiBackend.cpp
    src/Engine/Graphics/FrameProfiler.cpp
    src/Engine/Graphics/SceneRenderer.cpp
    src/Engine/Graphics/Lighting.cpp
    src/Engine/Graphics/PostProcess.cpp
    src/Engine/Graphics/Image.cpp
    src/Engine/Graphics/Geometry.cpp
    src/Engine/Assets/Image.cpp
    src/Engine/Assets/ImageExport.cpp
    src/Engine/Assets/Material.cpp
    src/Engine/Assets/Mesh.cpp
    src/Engine/Assets/Model.cpp
    src/Engine/Assets/ModelBundle.cpp
    src/Engine/World/Scene.cpp
    src/Engine/World/SceneFiles.cpp
    src/Engine/World/SceneContent.cpp
    src/Engine/World/SceneLinks.cpp
    src/Engine/World/SceneIdentity.cpp
    src/Engine/World/SceneSources.cpp
    src/Engine/World/SceneResolver.cpp
    src/Engine/Physics/World.cpp
    src/Engine/Physics/Body.cpp
    src/Engine/Physics/Character.cpp
    src/Engine/Physics/SceneSimulation.cpp
    src/Engine/Audio/Clip.cpp
    src/Engine/Audio/Mixer.cpp
    src/Engine/Audio/BackendMemory.cpp
    src/Engine/Audio/FrameQueue.cpp
    src/Engine/Audio/Output.cpp
    src/Engine/Audio/Playback.cpp
    src/Engine/Audio/Spatial.cpp
    src/Engine/Audio/Voice.cpp
    src/Engine/Audio/Source.cpp
    src/Engine/Audio/SceneAudio.cpp
    src/Engine/Script/Runtime.cpp
    src/Engine/Script/Compiler.cpp
    src/Engine/Script/Compilation.cpp
    src/Engine/Script/Modules.cpp
    src/Engine/Graphics/Environment.cpp
    src/Engine/Graphics/Shadows.cpp
    src/Engine/Graphics/ShadowAtlas.cpp
    src/Engine/Graphics/Occlusion.cpp
    src/Engine/Graphics/View.cpp
    src/Engine/Graphics/Overlay.cpp
    src/Engine/Graphics/Frustum.cpp
    src/Engine/Graphics/DrawBatches.cpp
    src/Engine/Graphics/RenderQueue.cpp
    src/Engine/Graphics/SpatialIndex.cpp
    src/Engine/Editor/Camera.cpp
    src/Engine/Editor/TransformTool.cpp
    src/Engine/Platform/ExternalFiles.cpp
    src/Engine/Editor/ScriptDiagnostics.cpp
    src/Engine/Script/SceneHost.cpp
    src/Engine/Script/InputSource.cpp
    src/Engine/Script/PhysicsSource.cpp
    src/Engine/Script/AudioSession.cpp
    src/Engine/Script/AnimationSession.cpp
    src/Engine/Script/Source.cpp
    src/Engine/Script/SceneScripts.cpp
    src/Engine/Script/LiveScripts.cpp
)
target_link_libraries(DotEngine PUBLIC DotBuildOptions Threads::Threads)
target_link_libraries(DotEngine PRIVATE SDL3::SDL3 glm::glm dawn::webgpu_dawn)
target_link_libraries(DotEngine PRIVATE imgui::imgui Freetype::Freetype)
target_link_libraries(DotEngine PRIVATE nlohmann_json::nlohmann_json tinyobjloader::tinyobjloader)
target_link_libraries(DotEngine PRIVATE EnTT::EnTT)
target_link_libraries(DotEngine PRIVATE Jolt::Jolt)
target_link_libraries(DotEngine PRIVATE DotAudioBackend DotImageBackend DotModelBackend)
target_include_directories(DotEngine SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")
target_link_libraries(DotEngine PRIVATE qjs)
target_include_directories(DotEngine SYSTEM PRIVATE "${MINIAUDIO_INCLUDE_DIRS}")
# Module producers and consumers must agree on compiler threading mode.
# 模块生产者与消费者必须使用一致的编译器线程模式。
target_compile_options(DotEngine PUBLIC "$<TARGET_PROPERTY:Jolt::Jolt,INTERFACE_COMPILE_OPTIONS>")
target_sources(DotEngine PUBLIC
    FILE_SET CXX_MODULES FILES
        src/Engine/Core/Types.cppm
        src/Engine/Graphics/Text.cppm
        src/Engine/Graphics/TextRenderer.cppm
        src/Engine/Script/Hud.cppm
        src/Engine/Script/GameClock.cppm
        src/Engine/Core/Text.cppm
        src/Engine/Core/Result.cppm
        src/Engine/Core/Logging.cppm
        src/Engine/Animation/Skin.cppm
        src/Engine/Animation/SkinFiles.cppm
        src/Engine/Animation/SceneSkin.cppm
        src/Engine/Animation/Skeleton.cppm
        src/Engine/Animation/Edit.cppm
        src/Engine/Animation/KeyGroup.cppm
        src/Engine/Assets/SceneSkins.cppm
        src/Engine/Animation/SkinSource.cppm
        src/Engine/Graphics/Skinning.cppm
        src/Engine/Animation/Clip.cppm
        src/Engine/Animation/Source.cppm
        src/Engine/Animation/ScenePlayers.cppm
        src/Engine/Animation/Files.cppm
        src/Engine/Animation/SceneAnimator.cppm
        src/Engine/Core/Clock.cppm
        src/Engine/Core/Memory.cppm
        src/Engine/Core/Process.cppm
        src/Engine/Core/Events.cppm
        src/Engine/Core/Files.cppm
        src/Engine/Core/Paths.cppm
        src/Engine/Core/StagedDirectory.cppm
        src/Engine/Core/Control.cppm
        src/Engine/Project/Project.cppm
        src/Engine/Project/GameSession.cppm
        src/Engine/Project/ScenePreparation.cppm
        src/Engine/Project/PlayerControl.cppm
        src/Engine/Game/State.cppm
        src/Engine/Game/Saves.cppm
        src/Engine/Script/SceneFlow.cppm
        src/Engine/Script/GameData.cppm
        src/Engine/Project/GamePackage.cppm
        src/Engine/Project/GameExport.cppm
        src/Engine/Project/Templates.cppm
        src/Engine/Editor/Document.cppm
        src/Engine/Editor/HierarchyTree.cppm
        src/Engine/Editor/Resources.cppm
        src/Engine/Editor/MaterialDocument.cppm
        src/Engine/Editor/AssetPreview.cppm
        src/Engine/Editor/PreviewWorker.cppm
        src/Engine/Editor/Play.cppm
        src/Engine/Platform/AgentInput.cppm
        src/Engine/Editor/ViewportCapture.cppm
        src/Engine/Editor/AnimationPreview.cppm
        src/Engine/Editor/AnimationDocument.cppm
        src/Engine/Editor/AnimationClipboard.cppm
        src/Engine/Editor/AnimationKeyMove.cppm
        src/Engine/Editor/SkeletonView.cppm
        src/Engine/Editor/Workspace.cppm
        src/Engine/Editor/Home.cppm
        src/Engine/Core/Core.cppm
        src/Engine/Platform/System.cppm
        src/Engine/Platform/UserData.cppm
        src/Engine/Platform/Window.cppm
        src/Engine/Platform/Events.cppm
        src/Engine/Platform/Dialog.cppm
        src/Engine/Platform/Input.cppm
        src/Engine/Core/Application.cppm
        src/Engine/Math/Math.cppm
        src/Engine/Graphics/Renderer.cppm
        src/Engine/Graphics/SceneRenderer.cppm
        src/Engine/Graphics/Lighting.cppm
        src/Engine/Graphics/PostProcess.cppm
        src/Engine/Graphics/Image.cppm
        src/Engine/Graphics/Geometry.cppm
        src/Engine/Assets/Image.cppm
        src/Engine/Assets/ImageExport.cppm
        src/Engine/Assets/Material.cppm
        src/Engine/Assets/Mesh.cppm
        src/Engine/Assets/Model.cppm
        src/Engine/Assets/ModelBundle.cppm
        src/Engine/World/Scene.cppm
        src/Engine/World/SceneFiles.cppm
        src/Engine/World/SceneContent.cppm
        src/Engine/World/SceneLinks.cppm
        src/Engine/World/SceneIdentity.cppm
        src/Engine/World/SceneSources.cppm
        src/Engine/World/SceneResolver.cppm
        src/Engine/Physics/World.cppm
        src/Engine/Physics/Body.cppm
        src/Engine/Physics/Character.cppm
        src/Engine/Physics/SceneSimulation.cppm
        src/Engine/Audio/Clip.cppm
        src/Engine/Audio/Mixer.cppm
        src/Engine/Audio/Output.cppm
        src/Engine/Audio/Playback.cppm
        src/Engine/Audio/Spatial.cppm
        src/Engine/Audio/Voice.cppm
        src/Engine/Audio/Source.cppm
        src/Engine/Audio/SceneAudio.cppm
        src/Engine/Script/Runtime.cppm
        src/Engine/Script/Host.cppm
        src/Engine/Script/SceneHost.cppm
        src/Engine/Script/InputSource.cppm
        src/Engine/Script/PhysicsSource.cppm
        src/Engine/Script/AudioSession.cppm
        src/Engine/Script/AnimationSession.cppm
        src/Engine/Script/Source.cppm
        src/Engine/Script/SceneScripts.cppm
        src/Engine/Script/LiveScripts.cppm
        src/Engine/Script/Compiler.cppm
        src/Engine/Script/Compilation.cppm
        src/Engine/Script/Modules.cppm
        src/Engine/Graphics/Environment.cppm
        src/Engine/Graphics/Shadows.cppm
        src/Engine/Graphics/ShadowAtlas.cppm
        src/Engine/Graphics/Occlusion.cppm
        src/Engine/Graphics/View.cppm
        src/Engine/Graphics/Overlay.cppm
        src/Engine/Graphics/Frustum.cppm
        src/Engine/Graphics/DrawBatches.cppm
        src/Engine/Graphics/RenderQueue.cppm
        src/Engine/Graphics/SpatialIndex.cppm
        src/Engine/Editor/Camera.cppm
        src/Engine/Editor/TransformTool.cppm
        src/Engine/Platform/ExternalFiles.cppm
        src/Engine/Editor/ScriptDiagnostics.cppm
)

include(cmake/ScriptModules.cmake)
include(cmake/Shaders.cmake)

add_executable(DotRuntime src/Runtime/main.cpp src/Runtime/ProjectPlayer.cpp src/Runtime/Options.cpp)
target_link_libraries(DotRuntime PRIVATE DotEngine SDL3::SDL3 nlohmann_json::nlohmann_json)

add_executable(DotGame src/Runtime/GameMain.cpp src/Runtime/ProjectPlayer.cpp src/Runtime/Options.cpp)
target_link_libraries(DotGame PRIVATE DotEngine SDL3::SDL3 nlohmann_json::nlohmann_json)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(DotGame PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
    target_link_options(DotGame PRIVATE "LINKER:--disable-new-dtags")
endif()
include(cmake/RuntimeKit.cmake)
add_custom_target(DotRuntimeFonts ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:DotRuntime>/resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DOT_EDITOR_FONT}" "$<TARGET_FILE_DIR:DotRuntime>/resources/EngineSans.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DOT_EDITOR_FONT_LICENSE}" "$<TARGET_FILE_DIR:DotRuntime>/resources/EngineSans-LICENSE.txt"
    VERBATIM)
add_dependencies(DotRuntime DotRuntimeFonts)
include(cmake/Examples.cmake)

add_executable(DotEditor src/Editor/main.cpp)
target_link_libraries(DotEditor PRIVATE DotEngine SDL3::SDL3 imgui::imgui nlohmann_json::nlohmann_json)
if(WIN32)
    set_target_properties(DotEditor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
add_dependencies(DotEditor DotRuntimeKit)
# Refresh owned resources even when code needs no relink; removed sources must not linger.
# 即使代码无需重新链接也刷新拥有的资源，已删除的源文件不能残留。
add_custom_target(DotEditorResources
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:DotEditor>/resources"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:DotEditor>/resources/script-sdk"
        "$<TARGET_FILE_DIR:DotEditor>/resources/templates"
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}/examples/PrismRelay"
        "-DDESTINATION_ROOT=$<TARGET_FILE_DIR:DotEditor>/resources/templates/PrismRelay"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyProject.cmake"
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}/examples/FeatureLab"
        "-DDESTINATION_ROOT=$<TARGET_FILE_DIR:DotEditor>/resources/templates/FeatureLab"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyProject.cmake"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/assets/environments" "$<TARGET_FILE_DIR:DotEditor>/resources/environments"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${DOT_TYPESCRIPT_TOOL_DIRECTORY}" "$<TARGET_FILE_DIR:DotEditor>/resources/typescript-native"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DOT_TYPESCRIPT_LICENSE}" "$<TARGET_FILE_DIR:DotEditor>/resources/typescript-native/LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DOT_EDITOR_FONT}" "$<TARGET_FILE_DIR:DotEditor>/resources/EditorSans.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DOT_EDITOR_FONT_LICENSE}" "$<TARGET_FILE_DIR:DotEditor>/resources/EditorSans-LICENSE.txt"
    VERBATIM)
add_dependencies(DotEditor DotEditorResources)

include(cmake/EditorDistribution.cmake)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json" SYMBOLIC COPY_ON_ERROR)
endif()
