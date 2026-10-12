find_package(doctest CONFIG REQUIRED)
add_executable(DotProcessFixture ProcessFixture.cpp)
target_link_libraries(DotProcessFixture PRIVATE DotBuildOptions nlohmann_json::nlohmann_json)
add_executable(DotTests
    main.cpp
    CoreTests.cpp
    TextTests.cpp
    EventTests.cpp
    FileTests.cpp
    InputTests.cpp
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
include(doctest)
doctest_discover_tests(DotTests PROPERTIES TIMEOUT 15 LABELS "unit" ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(DotProcessTests main.cpp ProcessTests.cpp)
target_link_libraries(DotProcessTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotProcessTests PRIVATE DOT_PROCESS_FIXTURE="$<TARGET_FILE:DotProcessFixture>")
add_dependencies(DotProcessTests DotProcessFixture)
doctest_discover_tests(DotProcessTests PROPERTIES TIMEOUT 15 LABELS "unit")

add_executable(DotCompilerFixture CompilerFixture.cpp)
target_link_libraries(DotCompilerFixture PRIVATE DotBuildOptions)
add_executable(DotCompilerTests main.cpp CompilerTests.cpp ScriptSdkTests.cpp SceneScriptsTests.cpp ReloadTests.cpp)
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

add_executable(DotGraphicsTests main.cpp GraphicsTests.cpp SceneRenderTests.cpp)
target_link_libraries(DotGraphicsTests PRIVATE DotEngine doctest::doctest)
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
