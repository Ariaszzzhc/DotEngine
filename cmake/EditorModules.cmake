# Editor authoring modules depend on the runtime engine, never the reverse.
# 编辑器创作模块依赖运行时引擎，运行时引擎不反向依赖编辑器。
add_library(DotEditorModules STATIC)
target_sources(DotEditorModules PRIVATE
    src/Editor/Modules/NavigationBake.cpp
    src/Editor/Modules/Document.cpp
    src/Editor/Modules/HierarchyTree.cpp
    src/Editor/Modules/Resources.cpp
    src/Editor/Modules/ModelReimport.cpp
    src/Editor/Modules/ModelImport.cpp
    src/Editor/Modules/MaterialDocument.cpp
    src/Editor/Modules/AssetPreview.cpp
    src/Editor/Modules/PreviewWorker.cpp
    src/Editor/Modules/Play.cpp
    src/Editor/Modules/ViewportCapture.cpp
    src/Editor/Modules/AnimationPreview.cpp
    src/Editor/Modules/AnimationCreation.cpp
    src/Editor/Modules/AnimationPosing.cpp
    src/Editor/Modules/AnimationRetarget.cpp
    src/Editor/Modules/AnimationDocument.cpp
    src/Editor/Modules/UiDocument.cpp
    src/Editor/Modules/UiSession.cpp
    src/Editor/Modules/ControllerDocument.cpp
    src/Editor/Modules/AnimationClipboard.cpp
    src/Editor/Modules/AnimationKeyMove.cpp
    src/Editor/Modules/AnimationTimeline.cpp
    src/Editor/Modules/AnimationFrames.cpp
    src/Editor/Modules/AnimationTangent.cpp
    src/Editor/Modules/SkeletonView.cpp
    src/Editor/Modules/Workspace.cpp
    src/Editor/Modules/Home.cpp
    src/Editor/Modules/Camera.cpp
    src/Editor/Modules/TransformTool.cpp
    src/Editor/Modules/ScriptDiagnostics.cpp
)
target_sources(DotEditorModules PUBLIC
    FILE_SET CXX_MODULES FILES
        src/Editor/Modules/NavigationBake.cppm
        src/Editor/Modules/Document.cppm
        src/Editor/Modules/HierarchyTree.cppm
        src/Editor/Modules/Resources.cppm
        src/Editor/Modules/ModelReimport.cppm
        src/Editor/Modules/ModelImport.cppm
        src/Editor/Modules/MaterialDocument.cppm
        src/Editor/Modules/AssetPreview.cppm
        src/Editor/Modules/PreviewWorker.cppm
        src/Editor/Modules/Play.cppm
        src/Editor/Modules/ViewportCapture.cppm
        src/Editor/Modules/AnimationPreview.cppm
        src/Editor/Modules/AnimationCreation.cppm
        src/Editor/Modules/AnimationPosing.cppm
        src/Editor/Modules/AnimationRetarget.cppm
        src/Editor/Modules/AnimationDocument.cppm
        src/Editor/Modules/UiDocument.cppm
        src/Editor/Modules/UiSession.cppm
        src/Editor/Modules/ControllerDocument.cppm
        src/Editor/Modules/AnimationClipboard.cppm
        src/Editor/Modules/AnimationKeyMove.cppm
        src/Editor/Modules/AnimationTimeline.cppm
        src/Editor/Modules/AnimationFrames.cppm
        src/Editor/Modules/AnimationTangent.cppm
        src/Editor/Modules/SkeletonView.cppm
        src/Editor/Modules/Workspace.cppm
        src/Editor/Modules/Home.cppm
        src/Editor/Modules/Camera.cppm
        src/Editor/Modules/TransformTool.cppm
        src/Editor/Modules/ScriptDiagnostics.cppm
)
target_link_libraries(DotEditorModules PUBLIC DotEngine)
target_link_libraries(DotEditorModules PRIVATE SDL3::SDL3 imgui::imgui nlohmann_json::nlohmann_json)
