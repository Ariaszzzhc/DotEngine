add_subdirectory(acceptance)

find_package(doctest CONFIG REQUIRED)
include(doctest)

# Set list-valued labels after discovery; doctest flattens PROPERTIES lists.
# 在发现用例后设置列表标签；doctest 会展平 PROPERTIES 中的列表值。
set(dot_test_labels "${CMAKE_CURRENT_BINARY_DIR}/DotTestLabels.cmake")
file(WRITE "${dot_test_labels}" "")
function(dotDiscoverTests target)
    cmake_parse_arguments(PARSE_ARGV 1 dot "" "LABELS" "PROPERTIES")
    doctest_discover_tests(${target} PROPERTIES ${dot_PROPERTIES})
    file(APPEND "${dot_test_labels}"
        "if(DEFINED ${target}_TESTS)\n"
        "    set_tests_properties(\${${target}_TESTS} PROPERTIES LABELS \"${dot_LABELS}\")\n"
        "endif()\n")
endfunction()

# Pure CPU behavior: no video driver, GPU, audio device, filesystem or compiler.
# 纯 CPU 行为：不初始化视频、GPU、音频设备、文件系统或编译器。
add_executable(DotUnitTests
    main.cpp
    CoreTests.cpp
    TextTests.cpp
    PathTests.cpp
    ExternalFileTests.cpp
    ScriptDiagnosticTests.cpp
    EventTests.cpp
    InputTests.cpp
    MathTests.cpp
    GeometryTests.cpp
    PostProcessTests.cpp
    LightingTests.cpp
    ShadowTests.cpp
    OcclusionTests.cpp
    ViewTests.cpp
    FrustumTests.cpp
    SpatialIndexTests.cpp
    DrawBatchTests.cpp
    RenderQueueTests.cpp
    AxisDragTests.cpp
    EnvironmentTests.cpp
    SceneTests.cpp
    SceneComponentTests.cpp
    SceneTransactionTests.cpp
    BodyTests.cpp
    MixerTests.cpp
    FrameQueueTests.cpp
    SpatialTests.cpp
    VoiceBatchTests.cpp
)
target_link_libraries(DotUnitTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
dotDiscoverTests(DotUnitTests LABELS "unit" PROPERTIES TIMEOUT 15)

# Native event translation and loop integration use SDL's dummy display.
# 原生事件转换与循环集成使用 SDL dummy 显示，不创建桌面窗口。
add_executable(DotPlatformTests main.cpp PlatformTests.cpp ApplicationTests.cpp)
target_link_libraries(DotPlatformTests PRIVATE DotEngine doctest::doctest SDL3::SDL3)
dotDiscoverTests(DotPlatformTests LABELS "integration;platform" PROPERTIES TIMEOUT 15
    ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(DotProcessFixture ProcessFixture.cpp)
target_link_libraries(DotProcessFixture PRIVATE DotBuildOptions nlohmann_json::nlohmann_json)
add_executable(DotTests
    main.cpp
    FileTests.cpp
    StagedDirectoryTests.cpp
    ProjectTests.cpp
    DocumentTests.cpp
    TransformToolTests.cpp
    ResourceTests.cpp
    MaterialDocumentTests.cpp
    PreviewWorkerTests.cpp
    InputHostTests.cpp
    PhysicsHostTests.cpp
    AudioHostTests.cpp
    AssetTests.cpp
    ImageTests.cpp
    MaterialTests.cpp
    ModelTests.cpp
    ModuleTests.cpp
    ExternalFileValidationTests.cpp
    SceneFileTests.cpp
    PhysicsTests.cpp
    PhysicsBatchTests.cpp
    ContactTests.cpp
    SceneContactTests.cpp
    ScenePhysicsTests.cpp
    RigidBodyTests.cpp
    AudioTests.cpp
    SceneAudioTests.cpp
    SceneAudioBindingTests.cpp
    ScriptRuntimeTests.cpp
    HostTests.cpp
    InstanceTests.cpp
    ScriptSourceTests.cpp
    SceneHostTests.cpp
)
target_compile_definitions(DotTests PRIVATE DOT_MODEL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/models")
target_link_libraries(DotTests PRIVATE DotEngine doctest::doctest SDL3::SDL3 nlohmann_json::nlohmann_json)
target_compile_definitions(DotTests PRIVATE DOT_IMAGE_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
dotDiscoverTests(DotTests LABELS "integration" PROPERTIES TIMEOUT 15)

add_executable(DotProcessTests main.cpp ProcessTests.cpp)
target_link_libraries(DotProcessTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotProcessTests PRIVATE DOT_PROCESS_FIXTURE="$<TARGET_FILE:DotProcessFixture>")
add_dependencies(DotProcessTests DotProcessFixture)
dotDiscoverTests(DotProcessTests LABELS "integration;process" PROPERTIES TIMEOUT 15)

add_executable(DotCompilerFixture CompilerFixture.cpp)
target_link_libraries(DotCompilerFixture PRIVATE DotBuildOptions)
add_executable(DotCompilerTests main.cpp CompilerTests.cpp ScriptSdkTests.cpp SceneScriptsTests.cpp ReloadTests.cpp InputSdkTests.cpp PhysicsSdkTests.cpp AudioSdkTests.cpp GameSessionTests.cpp PlayTests.cpp PublishedProgramTests.cpp GamePackageTests.cpp)
target_link_libraries(DotCompilerTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotCompilerTests PRIVATE
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_COMPILER_FIXTURE="$<TARGET_FILE:DotCompilerFixture>"
    DOT_PROCESS_FIXTURE="$<TARGET_FILE:DotProcessFixture>")
add_dependencies(DotCompilerTests DotCompilerFixture DotProcessFixture)
dotDiscoverTests(DotCompilerTests LABELS "integration;compiler" PROPERTIES TIMEOUT 30)

add_executable(DotAudioTests main.cpp OutputTests.cpp PlaybackTests.cpp SceneAudioNativeTests.cpp)
target_link_libraries(DotAudioTests PRIVATE DotEngine doctest::doctest)
dotDiscoverTests(DotAudioTests LABELS "integration;audio-native" PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_test(NAME runtime.audio_output COMMAND DotRuntimeAcceptance --audio-output-test)
set_tests_properties(runtime.audio_output PROPERTIES TIMEOUT 30 LABELS "audio-native" RUN_SERIAL TRUE)
add_test(NAME runtime.audio_playback COMMAND DotRuntimeAcceptance --audio-playback-test)
set_tests_properties(runtime.audio_playback PROPERTIES TIMEOUT 30 LABELS "audio-native" RUN_SERIAL TRUE)

add_test(NAME runtime.self_test COMMAND DotRuntimeAcceptance --self-test)
add_test(NAME runtime.version COMMAND DotRuntime --version)
set_tests_properties(runtime.self_test runtime.version PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.invalid_argument COMMAND DotRuntime --invalid)
set_tests_properties(runtime.invalid_argument PROPERTIES WILL_FAIL TRUE TIMEOUT 15)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
add_test(NAME runtime.material COMMAND DotRuntimeAcceptance --material-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.material PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.pbr COMMAND DotRuntimeAcceptance --pbr-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.pbr PROPERTIES TIMEOUT 60 LABELS "native;gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.hdr COMMAND DotRuntimeAcceptance --hdr-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.hdr PROPERTIES TIMEOUT 60 LABELS "native;gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.textures COMMAND DotRuntimeAcceptance --texture-test "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set_tests_properties(runtime.textures PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.surface_mesh COMMAND DotRuntimeAcceptance --surface-mesh-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.surface_mesh PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.files COMMAND DotRuntimeAcceptance --file-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.files PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.window COMMAND DotRuntimeAcceptance --window-smoke)
set_tests_properties(runtime.window PROPERTIES TIMEOUT 15 LABELS "acceptance" ENVIRONMENT "SDL_VIDEODRIVER=dummy")
add_test(NAME runtime.math COMMAND DotRuntimeAcceptance --math-test)
set_tests_properties(runtime.math PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.geometry COMMAND DotRuntimeAcceptance --geometry-test)
set_tests_properties(runtime.geometry PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.assets COMMAND DotRuntimeAcceptance --asset-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.assets PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene COMMAND DotRuntimeAcceptance --scene-test)
set_tests_properties(runtime.scene PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene_files COMMAND DotRuntimeAcceptance --scene-file-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.scene_files PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene_audio COMMAND DotRuntimeAcceptance --scene-audio-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.scene_audio PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene_audio_playback COMMAND DotRuntimeAcceptance --scene-audio-play-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.scene_audio_playback PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.script COMMAND DotRuntimeAcceptance --script-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.process COMMAND DotRuntimeAcceptance --process-test "$<TARGET_FILE:DotProcessFixture>" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.process PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.typescript COMMAND DotRuntimeAcceptance --typescript-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.typescript PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_host COMMAND DotRuntimeAcceptance --script-host-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_host PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_host_render COMMAND DotRuntimeAcceptance --script-host-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_host_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_instance COMMAND DotRuntimeAcceptance --script-instance-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_instance PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_components COMMAND DotRuntimeAcceptance --script-components-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_components PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_components_render COMMAND DotRuntimeAcceptance --script-components-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_components_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_reload COMMAND DotRuntimeAcceptance --script-reload-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_reload PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_reload_render COMMAND DotRuntimeAcceptance --script-reload-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_reload_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_authoring COMMAND DotRuntimeAcceptance --script-authoring-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_authoring PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_authoring_render COMMAND DotRuntimeAcceptance --script-authoring-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_authoring_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_input COMMAND DotRuntimeAcceptance --script-input-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_input PROPERTIES TIMEOUT 30 LABELS "acceptance" ENVIRONMENT "SDL_VIDEODRIVER=dummy")
add_test(NAME runtime.script_physics COMMAND DotRuntimeAcceptance --script-physics-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_physics PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_physics_render COMMAND DotRuntimeAcceptance --script-physics-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_physics_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_audio COMMAND DotRuntimeAcceptance --script-audio-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_audio PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_audio_play COMMAND DotRuntimeAcceptance --script-audio-play-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_audio_play PROPERTIES TIMEOUT 30 LABELS "audio-native" RUN_SERIAL TRUE)
add_test(NAME runtime.physics COMMAND DotRuntimeAcceptance --physics-test)
set_tests_properties(runtime.physics PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.audio_clips COMMAND DotRuntimeAcceptance --audio-clip-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.audio_clips PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.audio_mix COMMAND DotRuntimeAcceptance --audio-mix-test)
set_tests_properties(runtime.audio_mix PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.audio_batch COMMAND DotRuntimeAcceptance --audio-batch-test)
set_tests_properties(runtime.audio_batch PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.audio_spatial COMMAND DotRuntimeAcceptance --audio-spatial-test)
set_tests_properties(runtime.audio_spatial PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene_physics COMMAND DotRuntimeAcceptance --scene-physics-test)
set_tests_properties(runtime.scene_physics PROPERTIES TIMEOUT 15 LABELS "acceptance")

add_executable(DotGraphicsTests main.cpp GraphicsTests.cpp AssetPreviewTests.cpp AlphaGpuTests.cpp InstancingGpuTests.cpp VisibilityGpuTests.cpp PickingGpuTests.cpp OcclusionGpuTests.cpp ShadowGpuTests.cpp TextureTests.cpp EnvironmentGpuTests.cpp HdrTests.cpp PbrTests.cpp SceneMaterialTests.cpp SceneRenderTests.cpp ModelGpuTests.cpp GuiTests.cpp)
target_compile_definitions(DotGraphicsTests PRIVATE DOT_MODEL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/models")
target_link_libraries(DotGraphicsTests PRIVATE DotEngine doctest::doctest imgui::imgui)
target_compile_definitions(DotGraphicsTests PRIVATE DOT_ENVIRONMENT_ASSETS="${CMAKE_SOURCE_DIR}/assets/environments")
target_compile_definitions(DotGraphicsTests PRIVATE DOT_EDITOR_FONT="${DOT_EDITOR_FONT}")
target_compile_definitions(DotGraphicsTests PRIVATE DOT_IMAGE_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
dotDiscoverTests(DotGraphicsTests LABELS "integration;gpu;offscreen" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_executable(DotNativeGraphicsTests main.cpp NativeSurfaceTests.cpp)
target_link_libraries(DotNativeGraphicsTests PRIVATE DotEngine doctest::doctest)
dotDiscoverTests(DotNativeGraphicsTests LABELS "integration;gpu;native" PROPERTIES TIMEOUT 60
    RUN_SERIAL TRUE)

add_test(NAME runtime.gpu COMMAND DotRuntimeAcceptance --gpu-test)
set_tests_properties(runtime.gpu PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.mesh COMMAND DotRuntimeAcceptance --mesh-test)
set_tests_properties(runtime.mesh PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.scene_render COMMAND DotRuntimeAcceptance --scene-render-test)
set_tests_properties(runtime.scene_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.scene_physics_render COMMAND DotRuntimeAcceptance --scene-physics-render-test)
set_tests_properties(runtime.scene_physics_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.render COMMAND DotRuntimeAcceptance --render-smoke)
set_tests_properties(runtime.render PROPERTIES TIMEOUT 60 LABELS "native" RUN_SERIAL TRUE)

add_test(NAME editor.invalid_argument COMMAND DotEditor --invalid)
set_tests_properties(editor.invalid_argument PROPERTIES WILL_FAIL TRUE TIMEOUT 15)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
add_test(NAME editor.gui COMMAND DotEditorAcceptance --gui-test "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.gui PROPERTIES TIMEOUT 60 LABELS "gpu;native" RUN_SERIAL TRUE)
add_test(NAME editor.document COMMAND DotEditorAcceptance --document-test "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.document PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME editor.document_render COMMAND DotEditorAcceptance --document-render-test "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.document_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.project_player COMMAND DotRuntimeAcceptance --project-player-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(runtime.project_player PROPERTIES TIMEOUT 60 LABELS "native;gpu;audio-native" RUN_SERIAL TRUE)
add_test(NAME runtime.project_bad_budget COMMAND DotRuntime --project missing missing -1)
set_tests_properties(runtime.project_bad_budget PROPERTIES WILL_FAIL TRUE TIMEOUT 15 LABELS "acceptance")
add_test(NAME editor.project COMMAND DotEditorAcceptance --project-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.project PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME editor.project_render COMMAND DotEditorAcceptance --project-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.project_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)

add_executable(DotEditorTests main.cpp EditorCommandTests.cpp)
target_link_libraries(DotEditorTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotEditorTests PRIVATE
    DOT_GAME_EXECUTABLE="$<TARGET_FILE:DotGame>"
    DOT_EDITOR_EXECUTABLE="$<TARGET_FILE:DotEditor>"
    DOT_RUNTIME_EXECUTABLE="$<TARGET_FILE:DotRuntime>"
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotEditorTests DotEditor DotRuntime DotGame)
dotDiscoverTests(DotEditorTests LABELS "integration;compiler" PROPERTIES TIMEOUT 30)

add_test(NAME editor.workspace COMMAND DotEditorAcceptance --workspace-test "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.workspace PROPERTIES TIMEOUT 60 LABELS "gpu;native" RUN_SERIAL TRUE)

add_test(NAME editor.viewport COMMAND DotEditorAcceptance --viewport-test "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.viewport PROPERTIES TIMEOUT 60 LABELS "acceptance;gpu;native" RUN_SERIAL TRUE)

add_executable(DotWorkspaceTests main.cpp WorkspaceTests.cpp WorkspaceLayoutTests.cpp WorkspaceMoveTests.cpp AssetBrowserTests.cpp)
add_dependencies(DotWorkspaceTests DotEditorResources)
target_link_libraries(DotWorkspaceTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json SDL3::SDL3)
target_compile_definitions(DotWorkspaceTests PRIVATE
    DOT_EDITOR_FONT="${DOT_EDITOR_FONT}"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
dotDiscoverTests(DotWorkspaceTests LABELS "acceptance;gpu;native" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

add_test(NAME runtime.exported_game COMMAND DotRuntimeAcceptance --game-player-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(runtime.exported_game PROPERTIES TIMEOUT 60 LABELS "native;gpu;audio-native" RUN_SERIAL TRUE)

add_executable(DotExportTests main.cpp GameExportTests.cpp)
target_link_libraries(DotExportTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotExportTests PRIVATE
    DOT_CMAKE_EXECUTABLE="${CMAKE_COMMAND}"
    DOT_RUNTIME_MANIFEST_SCRIPT="${CMAKE_SOURCE_DIR}/cmake/WriteRuntimeManifest.cmake"
    DOT_RUNTIME_ACCEPTANCE="$<TARGET_FILE:DotRuntimeAcceptance>"
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotExportTests DotRuntimeKit DotRuntimeAcceptance)
dotDiscoverTests(DotExportTests LABELS "acceptance;process;compiler;gpu;native" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

add_executable(DotExampleTests main.cpp ExampleTests.cpp)
target_link_libraries(DotExampleTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotExampleTests PRIVATE
    DOT_EXAMPLE_PROJECT="${CMAKE_BINARY_DIR}/examples/PrismRelay"
    DOT_EXAMPLE_SOURCE="${CMAKE_SOURCE_DIR}/examples/PrismRelay"
    DOT_LAB_SOURCE="${CMAKE_SOURCE_DIR}/acceptance/FeatureLab"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotExampleTests DotExamples)
dotDiscoverTests(DotExampleTests LABELS "integration;compiler" PROPERTIES TIMEOUT 60)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/example_acceptance")
add_executable(DotExampleNativeTests main.cpp ExampleNativeTests.cpp)
target_link_libraries(DotExampleNativeTests PRIVATE DotEngine doctest::doctest SDL3::SDL3)
target_compile_definitions(DotExampleNativeTests PRIVATE
    DOT_EXAMPLE_CAPTURE_ROOT="${CMAKE_CURRENT_BINARY_DIR}/example_acceptance"
    DOT_EXAMPLE_PROJECT="${CMAKE_BINARY_DIR}/examples/PrismRelay"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit")
add_dependencies(DotExampleNativeTests DotExamples DotRuntimeKit)
dotDiscoverTests(DotExampleNativeTests LABELS "acceptance;gpu;native" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

add_executable(DotFeatureLabTests main.cpp FeatureLabTests.cpp)
target_link_libraries(DotFeatureLabTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotFeatureLabTests PRIVATE
    DOT_FEATURE_LAB="${CMAKE_BINARY_DIR}/acceptance/FeatureLab"
    DOT_SCRIPT_SDK_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/scripts"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotFeatureLabTests DotFeatureLab)
dotDiscoverTests(DotFeatureLabTests LABELS "integration;compiler" PROPERTIES TIMEOUT 60)

add_executable(DotFeatureLabNativeTests main.cpp FeatureLabNativeTests.cpp)
target_link_libraries(DotFeatureLabNativeTests PRIVATE DotEngine doctest::doctest SDL3::SDL3 nlohmann_json::nlohmann_json)
target_compile_definitions(DotFeatureLabNativeTests PRIVATE
    DOT_FEATURE_LAB="${CMAKE_BINARY_DIR}/acceptance/FeatureLab"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    DOT_EDITOR_FONT="${DOT_EDITOR_FONT}")
add_dependencies(DotFeatureLabNativeTests DotFeatureLab DotRuntimeKit DotEditor)
dotDiscoverTests(DotFeatureLabNativeTests LABELS "acceptance;gpu;native" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

add_executable(DotTemplateTests main.cpp TemplateTests.cpp)
target_link_libraries(DotTemplateTests PRIVATE DotEngine doctest::doctest)
target_compile_definitions(DotTemplateTests PRIVATE
    DOT_TEMPLATE_PROJECT="${CMAKE_BINARY_DIR}/examples/PrismRelay"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotTemplateTests DotExamples)
dotDiscoverTests(DotTemplateTests LABELS "integration;compiler" PROPERTIES TIMEOUT 30)

add_executable(DotHomeTests main.cpp HomeTests.cpp)
target_link_libraries(DotHomeTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotHomeTests PRIVATE DOT_EDITOR_RESOURCES="$<TARGET_FILE_DIR:DotEditor>/resources")
add_dependencies(DotHomeTests DotEditor)
dotDiscoverTests(DotHomeTests LABELS "acceptance;gpu;native" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

add_test(NAME editor.home_smoke COMMAND DotEditor home 3)
set_tests_properties(editor.home_smoke PROPERTIES TIMEOUT 60 LABELS "gpu;native" RUN_SERIAL TRUE)

add_executable(DotEditorDistributionTests main.cpp EditorDistributionTests.cpp)
target_link_libraries(DotEditorDistributionTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotEditorDistributionTests PRIVATE
    DOT_EDITOR_DISTRIBUTION="${CMAKE_BINARY_DIR}/distribution/DotEngine")
add_dependencies(DotEditorDistributionTests DotEditorDistribution)
dotDiscoverTests(DotEditorDistributionTests LABELS "acceptance;gpu;native" PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# Executable workflow checks are acceptance cases, independent of device labels.
# 可执行程序流程检查统一为验收用例，设备标签独立保留。
get_property(dot_acceptance_tests DIRECTORY PROPERTY TESTS)
foreach(dot_test IN LISTS dot_acceptance_tests)
    set_property(TEST "${dot_test}" APPEND PROPERTY LABELS "acceptance")
endforeach()

set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${dot_test_labels}")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/environment_acceptance")
add_test(NAME runtime.environment COMMAND DotRuntimeAcceptance --environment-test
    "${CMAKE_SOURCE_DIR}/assets/environments/studio_small_09_1k.hdr"
    "${CMAKE_CURRENT_BINARY_DIR}/environment_acceptance")
set_tests_properties(runtime.environment PROPERTIES TIMEOUT 60 LABELS "acceptance;gpu;offscreen" RUN_SERIAL TRUE)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shadow_acceptance")
add_test(NAME runtime.shadows COMMAND DotRuntimeAcceptance --shadow-test
    "${CMAKE_CURRENT_BINARY_DIR}/shadow_acceptance")
set_tests_properties(runtime.shadows PROPERTIES TIMEOUT 60 LABELS "acceptance;gpu;offscreen" RUN_SERIAL TRUE)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/occlusion_acceptance")
add_test(NAME runtime.occlusion COMMAND DotRuntimeAcceptance --occlusion-test
    "${CMAKE_CURRENT_BINARY_DIR}/occlusion_acceptance")
set_tests_properties(runtime.occlusion PROPERTIES TIMEOUT 60 LABELS "acceptance;gpu;offscreen" RUN_SERIAL TRUE)

add_test(NAME distribution.archive_excludes_finder_metadata
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/archive_acceptance"
        "-DARCHIVE_SCRIPT=${CMAKE_SOURCE_DIR}/cmake/ArchiveEditor.cmake"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/EditorArchiveTests.cmake")
set_tests_properties(distribution.archive_excludes_finder_metadata PROPERTIES
    LABELS "integration" TIMEOUT 15)

add_test(NAME editor.model_import COMMAND DotEditorAcceptance --model-import-test "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.model_import PROPERTIES TIMEOUT 60 LABELS "acceptance;gpu;native" RUN_SERIAL TRUE)
