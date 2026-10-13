find_package(doctest CONFIG REQUIRED)
add_executable(DotProcessFixture ProcessFixture.cpp)
target_link_libraries(DotProcessFixture PRIVATE DotBuildOptions nlohmann_json::nlohmann_json)
add_executable(DotTests
    main.cpp
    CoreTests.cpp
    TextTests.cpp
    PathTests.cpp
    EventTests.cpp
    FileTests.cpp
    StagedDirectoryTests.cpp
    ProjectTests.cpp
    DocumentTests.cpp
    ResourceTests.cpp
    InputTests.cpp
    InputHostTests.cpp
    PhysicsHostTests.cpp
    AudioHostTests.cpp
    PlatformTests.cpp
    ApplicationTests.cpp
    MathTests.cpp
    GeometryTests.cpp
    AssetTests.cpp
    SceneTests.cpp
    SceneComponentTests.cpp
    SceneTransactionTests.cpp
    SceneFileTests.cpp
    PhysicsTests.cpp
    PhysicsBatchTests.cpp
    ContactTests.cpp
    SceneContactTests.cpp
    ScenePhysicsTests.cpp
    BodyTests.cpp
    RigidBodyTests.cpp
    AudioTests.cpp
    MixerTests.cpp
    FrameQueueTests.cpp
    SpatialTests.cpp
    SceneAudioTests.cpp
    VoiceBatchTests.cpp
    SceneAudioBindingTests.cpp
    ScriptRuntimeTests.cpp
    HostTests.cpp
    InstanceTests.cpp
    ScriptSourceTests.cpp
    SceneHostTests.cpp
)
target_link_libraries(DotTests PRIVATE DotEngine doctest::doctest SDL3::SDL3 nlohmann_json::nlohmann_json)
target_compile_definitions(DotTests PRIVATE DOT_SCRIPT_SDK_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/scripts")
include(doctest)
doctest_discover_tests(DotTests PROPERTIES TIMEOUT 15 LABELS "unit" ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(DotProcessTests main.cpp ProcessTests.cpp)
target_link_libraries(DotProcessTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotProcessTests PRIVATE DOT_PROCESS_FIXTURE="$<TARGET_FILE:DotProcessFixture>")
add_dependencies(DotProcessTests DotProcessFixture)
doctest_discover_tests(DotProcessTests PROPERTIES TIMEOUT 15 LABELS "unit")

add_executable(DotCompilerFixture CompilerFixture.cpp)
target_link_libraries(DotCompilerFixture PRIVATE DotBuildOptions)
add_executable(DotCompilerTests main.cpp CompilerTests.cpp ScriptSdkTests.cpp SceneScriptsTests.cpp ReloadTests.cpp InputSdkTests.cpp PhysicsSdkTests.cpp AudioSdkTests.cpp GameSessionTests.cpp PlayTests.cpp PublishedProgramTests.cpp GamePackageTests.cpp)
target_link_libraries(DotCompilerTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotCompilerTests PRIVATE
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_SCRIPT_SDK_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/scripts"
    DOT_COMPILER_FIXTURE="$<TARGET_FILE:DotCompilerFixture>"
    DOT_PROCESS_FIXTURE="$<TARGET_FILE:DotProcessFixture>")
add_dependencies(DotCompilerTests DotCompilerFixture DotProcessFixture)
doctest_discover_tests(DotCompilerTests PROPERTIES TIMEOUT 30 LABELS "unit")

add_executable(DotAudioTests main.cpp OutputTests.cpp PlaybackTests.cpp SceneAudioNativeTests.cpp)
target_link_libraries(DotAudioTests PRIVATE DotEngine doctest::doctest)
doctest_discover_tests(DotAudioTests PROPERTIES TIMEOUT 30 LABELS "audio-native" RUN_SERIAL TRUE)
add_test(NAME runtime.audio_output COMMAND DotRuntime --audio-output-test)
set_tests_properties(runtime.audio_output PROPERTIES TIMEOUT 30 LABELS "audio-native" RUN_SERIAL TRUE)
add_test(NAME runtime.audio_playback COMMAND DotRuntime --audio-playback-test)
set_tests_properties(runtime.audio_playback PROPERTIES TIMEOUT 30 LABELS "audio-native" RUN_SERIAL TRUE)

add_test(NAME runtime.self_test COMMAND DotRuntime --self-test)
add_test(NAME runtime.version COMMAND DotRuntime --version)
set_tests_properties(runtime.self_test runtime.version PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.invalid_argument COMMAND DotRuntime --invalid)
set_tests_properties(runtime.invalid_argument PROPERTIES WILL_FAIL TRUE TIMEOUT 15)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
add_test(NAME runtime.files COMMAND DotRuntime --file-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.files PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.window COMMAND DotRuntime --window-smoke)
set_tests_properties(runtime.window PROPERTIES TIMEOUT 15 LABELS "acceptance" ENVIRONMENT "SDL_VIDEODRIVER=dummy")
add_test(NAME runtime.math COMMAND DotRuntime --math-test)
set_tests_properties(runtime.math PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.geometry COMMAND DotRuntime --geometry-test)
set_tests_properties(runtime.geometry PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.assets COMMAND DotRuntime --asset-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.assets PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene COMMAND DotRuntime --scene-test)
set_tests_properties(runtime.scene PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene_files COMMAND DotRuntime --scene-file-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.scene_files PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene_audio COMMAND DotRuntime --scene-audio-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.scene_audio PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene_audio_playback COMMAND DotRuntime --scene-audio-play-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.scene_audio_playback PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.script COMMAND DotRuntime --script-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.process COMMAND DotRuntime --process-test "$<TARGET_FILE:DotProcessFixture>" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.process PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.typescript COMMAND DotRuntime --typescript-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.typescript PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_host COMMAND DotRuntime --script-host-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_host PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_host_render COMMAND DotRuntime --script-host-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_host_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_instance COMMAND DotRuntime --script-instance-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_instance PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_components COMMAND DotRuntime --script-components-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_components PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_components_render COMMAND DotRuntime --script-components-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_components_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_reload COMMAND DotRuntime --script-reload-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_reload PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_reload_render COMMAND DotRuntime --script-reload-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_reload_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_authoring COMMAND DotRuntime --script-authoring-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_authoring PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_authoring_render COMMAND DotRuntime --script-authoring-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_authoring_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_input COMMAND DotRuntime --script-input-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_input PROPERTIES TIMEOUT 30 LABELS "acceptance" ENVIRONMENT "SDL_VIDEODRIVER=dummy")
add_test(NAME runtime.script_physics COMMAND DotRuntime --script-physics-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_physics PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_physics_render COMMAND DotRuntime --script-physics-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_physics_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.script_audio COMMAND DotRuntime --script-audio-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_audio PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME runtime.script_audio_play COMMAND DotRuntime --script-audio-play-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.script_audio_play PROPERTIES TIMEOUT 30 LABELS "audio-native" RUN_SERIAL TRUE)
add_test(NAME runtime.physics COMMAND DotRuntime --physics-test)
set_tests_properties(runtime.physics PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.audio_clips COMMAND DotRuntime --audio-clip-test "${CMAKE_CURRENT_BINARY_DIR}/file_acceptance")
set_tests_properties(runtime.audio_clips PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.audio_mix COMMAND DotRuntime --audio-mix-test)
set_tests_properties(runtime.audio_mix PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.audio_batch COMMAND DotRuntime --audio-batch-test)
set_tests_properties(runtime.audio_batch PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.audio_spatial COMMAND DotRuntime --audio-spatial-test)
set_tests_properties(runtime.audio_spatial PROPERTIES TIMEOUT 15 LABELS "acceptance")
add_test(NAME runtime.scene_physics COMMAND DotRuntime --scene-physics-test)
set_tests_properties(runtime.scene_physics PROPERTIES TIMEOUT 15 LABELS "acceptance")

add_executable(DotGraphicsTests main.cpp GraphicsTests.cpp SceneRenderTests.cpp GuiTests.cpp)
target_link_libraries(DotGraphicsTests PRIVATE DotEngine doctest::doctest imgui::imgui)
doctest_discover_tests(DotGraphicsTests PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.gpu COMMAND DotRuntime --gpu-test)
set_tests_properties(runtime.gpu PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.mesh COMMAND DotRuntime --mesh-test)
set_tests_properties(runtime.mesh PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.scene_render COMMAND DotRuntime --scene-render-test)
set_tests_properties(runtime.scene_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.scene_physics_render COMMAND DotRuntime --scene-physics-render-test)
set_tests_properties(runtime.scene_physics_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.render COMMAND DotRuntime --render-smoke)
set_tests_properties(runtime.render PROPERTIES TIMEOUT 60 LABELS "native" RUN_SERIAL TRUE)

add_test(NAME editor.invalid_argument COMMAND DotEditor --invalid)
set_tests_properties(editor.invalid_argument PROPERTIES WILL_FAIL TRUE TIMEOUT 15)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
add_test(NAME editor.gui COMMAND DotEditor --gui-test "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.gui PROPERTIES TIMEOUT 60 LABELS "gpu;native" RUN_SERIAL TRUE)
add_test(NAME editor.document COMMAND DotEditor --document-test "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.document PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME editor.document_render COMMAND DotEditor --document-render-test "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.document_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.project_player COMMAND DotRuntime --project-player-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(runtime.project_player PROPERTIES TIMEOUT 60 LABELS "native;gpu;audio-native" RUN_SERIAL TRUE)
add_test(NAME runtime.project_bad_budget COMMAND DotRuntime --project missing missing -1)
set_tests_properties(runtime.project_bad_budget PROPERTIES WILL_FAIL TRUE TIMEOUT 15 LABELS "acceptance")
add_test(NAME editor.project COMMAND DotEditor --project-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.project PROPERTIES TIMEOUT 30 LABELS "acceptance")
add_test(NAME editor.project_render COMMAND DotEditor --project-render-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.project_render PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)

add_executable(DotEditorTests main.cpp EditorCommandTests.cpp)
target_link_libraries(DotEditorTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotEditorTests PRIVATE
    DOT_EDITOR_EXECUTABLE="$<TARGET_FILE:DotEditor>"
    DOT_RUNTIME_EXECUTABLE="$<TARGET_FILE:DotRuntime>"
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    DOT_SCRIPT_SDK_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/scripts"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotEditorTests DotEditor DotRuntime)
doctest_discover_tests(DotEditorTests PROPERTIES TIMEOUT 30 LABELS "unit")

add_test(NAME editor.workspace COMMAND DotEditor --workspace-test "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(editor.workspace PROPERTIES TIMEOUT 60 LABELS "gpu;native" RUN_SERIAL TRUE)

add_executable(DotWorkspaceTests main.cpp WorkspaceTests.cpp)
target_link_libraries(DotWorkspaceTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotWorkspaceTests PRIVATE
    DOT_EDITOR_FONT="${DOT_EDITOR_FONT}"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_SCRIPT_SDK_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/scripts")
doctest_discover_tests(DotWorkspaceTests PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)

add_test(NAME runtime.exported_game COMMAND DotRuntime --game-player-test "${DOT_TYPESCRIPT_COMPILER}" "${CMAKE_SOURCE_DIR}/assets/scripts" "${CMAKE_CURRENT_BINARY_DIR}/project_acceptance")
set_tests_properties(runtime.exported_game PROPERTIES TIMEOUT 60 LABELS "native;gpu;audio-native" RUN_SERIAL TRUE)

add_executable(DotExportTests main.cpp GameExportTests.cpp)
target_link_libraries(DotExportTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotExportTests PRIVATE
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_SCRIPT_SDK_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/scripts")
add_dependencies(DotExportTests DotRuntimeKit)
doctest_discover_tests(DotExportTests PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)

add_executable(DotExampleTests main.cpp ExampleTests.cpp)
target_link_libraries(DotExampleTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotExampleTests PRIVATE
    DOT_EXAMPLE_PROJECT="${CMAKE_BINARY_DIR}/examples/PrismRelay"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotExampleTests DotExamples)
doctest_discover_tests(DotExampleTests PROPERTIES TIMEOUT 60 LABELS "unit")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/example_acceptance")
add_executable(DotExampleNativeTests main.cpp ExampleNativeTests.cpp)
target_link_libraries(DotExampleNativeTests PRIVATE DotEngine doctest::doctest SDL3::SDL3)
target_compile_definitions(DotExampleNativeTests PRIVATE
    DOT_EXAMPLE_CAPTURE_ROOT="${CMAKE_CURRENT_BINARY_DIR}/example_acceptance"
    DOT_EXAMPLE_PROJECT="${CMAKE_BINARY_DIR}/examples/PrismRelay"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit")
add_dependencies(DotExampleNativeTests DotExamples DotRuntimeKit)
doctest_discover_tests(DotExampleNativeTests PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)

add_executable(DotFeatureLabTests main.cpp FeatureLabTests.cpp)
target_link_libraries(DotFeatureLabTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotFeatureLabTests PRIVATE
    DOT_FEATURE_LAB="${CMAKE_BINARY_DIR}/acceptance/FeatureLab"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotFeatureLabTests DotFeatureLab)
doctest_discover_tests(DotFeatureLabTests PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(DotFeatureLabNativeTests main.cpp FeatureLabNativeTests.cpp)
target_link_libraries(DotFeatureLabNativeTests PRIVATE DotEngine doctest::doctest SDL3::SDL3 nlohmann_json::nlohmann_json)
target_compile_definitions(DotFeatureLabNativeTests PRIVATE
    DOT_FEATURE_LAB="${CMAKE_BINARY_DIR}/acceptance/FeatureLab"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    DOT_EDITOR_FONT="${DOT_EDITOR_FONT}")
add_dependencies(DotFeatureLabNativeTests DotFeatureLab DotRuntimeKit DotEditor)
doctest_discover_tests(DotFeatureLabNativeTests PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)

add_executable(DotTemplateTests main.cpp TemplateTests.cpp)
target_link_libraries(DotTemplateTests PRIVATE DotEngine doctest::doctest)
target_compile_definitions(DotTemplateTests PRIVATE
    DOT_TEMPLATE_PROJECT="${CMAKE_BINARY_DIR}/examples/PrismRelay"
    DOT_SCRIPT_SDK_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/scripts"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotTemplateTests DotExamples)
doctest_discover_tests(DotTemplateTests PROPERTIES TIMEOUT 30 LABELS "unit")
