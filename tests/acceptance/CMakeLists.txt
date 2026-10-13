# Acceptance drivers are development-only consumers of the same engine/player.
# 验收驱动仅用于开发，消费相同的引擎与播放器。
add_executable(DotRuntimeAcceptance
    Runtime/main.cpp
    Runtime/AudioAcceptance.cpp
    Runtime/AudioScriptAcceptance.cpp
    Runtime/AuthoringAcceptance.cpp
    Runtime/CompilerAcceptance.cpp
    Runtime/ComponentsAcceptance.cpp
    Runtime/HostAcceptance.cpp
    Runtime/InputAcceptance.cpp
    Runtime/InstanceAcceptance.cpp
    Runtime/PhysicsAcceptance.cpp
    Runtime/PhysicsScriptAcceptance.cpp
    Runtime/ProcessAcceptance.cpp
    Runtime/ProjectPlayerAcceptance.cpp
    Runtime/ReloadAcceptance.cpp
    Runtime/SceneAudioAcceptance.cpp
    Runtime/ScriptAcceptance.cpp
    Runtime/SurfaceMeshAcceptance.cpp
    Runtime/TextureAcceptance.cpp
    "${PROJECT_SOURCE_DIR}/src/Runtime/ProjectPlayer.cpp")
target_link_libraries(DotRuntimeAcceptance PRIVATE DotEngine SDL3::SDL3 nlohmann_json::nlohmann_json)

add_executable(DotEditorAcceptance
    Editor/main.cpp
    Editor/DocumentAcceptance.cpp
    Editor/GuiAcceptance.cpp
    Editor/ProjectAcceptance.cpp
    Editor/WorkspaceAcceptance.cpp)
target_link_libraries(DotEditorAcceptance PRIVATE DotEngine SDL3::SDL3 imgui::imgui nlohmann_json::nlohmann_json)
