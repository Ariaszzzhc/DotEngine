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
    PlayInputTests.cpp
    ViewportCaptureTests.cpp
    MathTests.cpp
    GeometryTests.cpp
    MeshCodecTests.cpp
    PostProcessTests.cpp
    LightingTests.cpp
    ShadowTests.cpp
    ShadowAtlasTests.cpp
    OcclusionTests.cpp
    ViewTests.cpp
    OverlayTests.cpp
    SkeletonTests.cpp
    FrustumTests.cpp
    SpatialIndexTests.cpp
    DrawBatchTests.cpp
    RenderQueueTests.cpp
    AxisDragTests.cpp
    AnimationClipTests.cpp
    AnimationEditTests.cpp
    AnimationKeyGroupTests.cpp
    AnimationClipboardTests.cpp
    AnimationKeyMoveTests.cpp
    AnimationPlayerTests.cpp
    HomeDialogTests.cpp
    SceneFlowTests.cpp
    GameStateTests.cpp
    GameDataTests.cpp
    SceneAnimatorTests.cpp
    AnimationTransitionTests.cpp
    SkinTests.cpp
    SkinBindingTests.cpp
    EnvironmentTests.cpp
    SceneTests.cpp
    HierarchyTests.cpp
    SceneContentTests.cpp
    SceneLinkTests.cpp
    SceneOverrideTests.cpp
    SceneSourceTests.cpp
    SceneResolverTests.cpp
    SceneComponentTests.cpp
    SceneTransactionTests.cpp
    BodyTests.cpp
    CharacterTests.cpp
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
    SaveTests.cpp
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
    ImageExportTests.cpp
    AnimationPreviewTests.cpp
    AnimationDocumentTests.cpp
    SkeletonViewTests.cpp
    MaterialTests.cpp
    AnimationFileTests.cpp
    SkinFileTests.cpp
    ModelTests.cpp
    ModelAnimationTests.cpp
    ModelSkinTests.cpp
    ModuleTests.cpp
    ExternalFileValidationTests.cpp
    SceneFileTests.cpp
    PhysicsTests.cpp
    SphereCastTests.cpp
    CharacterPhysicsTests.cpp
    CharacterBatchTests.cpp
    PhysicsBatchTests.cpp
    ContactTests.cpp
    SceneContactTests.cpp
    ScenePhysicsTests.cpp
    SceneCharacterTests.cpp
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
add_executable(DotCompilerTests main.cpp CompilerTests.cpp ScriptSdkTests.cpp SceneScriptsTests.cpp ReloadTests.cpp InputSdkTests.cpp PhysicsSdkTests.cpp AudioSdkTests.cpp GameSessionTests.cpp SceneTransitionTests.cpp ScenePlayerSessionTests.cpp AnimationSdkTests.cpp PlayTests.cpp PublishedProgramTests.cpp GamePackageTests.cpp)
target_link_libraries(DotCompilerTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotCompilerTests PRIVATE
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}"
    DOT_COMPILER_FIXTURE="$<TARGET_FILE:DotCompilerFixture>"
    DOT_PROCESS_FIXTURE="$<TARGET_FILE:DotProcessFixture>")
add_dependencies(DotCompilerTests DotCompilerFixture DotProcessFixture)
dotDiscoverTests(DotCompilerTests LABELS "integration;compiler" PROPERTIES TIMEOUT 30)

add_executable(DotAudioTests main.cpp OutputTests.cpp PlaybackTests.cpp SceneAudioNativeTests.cpp)
target_link_libraries(DotAudioTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
dotDiscoverTests(DotAudioTests LABELS "integration;audio-native" PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
# Product command contracts are headless; visual QA uses the real editor.
# 产品命令契约不打开窗口，可视化检查使用真实编辑器。
add_test(NAME runtime.version COMMAND DotRuntime --version)
set_tests_properties(runtime.version PROPERTIES TIMEOUT 15 LABELS "integration")
add_test(NAME runtime.invalid_argument COMMAND DotRuntime --invalid)
add_test(NAME editor.invalid_argument COMMAND DotEditor --invalid)
set_tests_properties(runtime.invalid_argument editor.invalid_argument PROPERTIES
    WILL_FAIL TRUE TIMEOUT 15 LABELS "integration")

add_executable(DotGraphicsTests main.cpp SceneSkinGpuTests.cpp SkinGpuTests.cpp AnimationGpuTests.cpp GraphicsTests.cpp AssetPreviewTests.cpp AlphaGpuTests.cpp InstancingGpuTests.cpp VisibilityGpuTests.cpp PickingGpuTests.cpp OcclusionGpuTests.cpp ShadowGpuTests.cpp TextureTests.cpp EnvironmentGpuTests.cpp HdrTests.cpp PbrTests.cpp SceneMaterialTests.cpp SceneRenderTests.cpp ModelGpuTests.cpp GuiTests.cpp)
target_compile_definitions(DotGraphicsTests PRIVATE DOT_MODEL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/models")
target_link_libraries(DotGraphicsTests PRIVATE DotEngine doctest::doctest imgui::imgui)
target_compile_definitions(DotGraphicsTests PRIVATE DOT_ENVIRONMENT_ASSETS="${CMAKE_SOURCE_DIR}/assets/environments")
target_compile_definitions(DotGraphicsTests PRIVATE DOT_EDITOR_FONT="${DOT_EDITOR_FONT}")
target_compile_definitions(DotGraphicsTests PRIVATE DOT_IMAGE_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
dotDiscoverTests(DotGraphicsTests LABELS "integration;gpu;offscreen" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

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

add_executable(DotExportTests main.cpp GameExportTests.cpp)
target_link_libraries(DotExportTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotExportTests PRIVATE
    DOT_CMAKE_EXECUTABLE="${CMAKE_COMMAND}"
    DOT_RUNTIME_MANIFEST_SCRIPT="${CMAKE_SOURCE_DIR}/cmake/WriteRuntimeManifest.cmake"
    DOT_RUNTIME_KIT="$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotExportTests DotRuntimeKit)
dotDiscoverTests(DotExportTests LABELS "integration;process;compiler" PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)

add_executable(DotExampleTests main.cpp ExampleTests.cpp ShoulderCameraTests.cpp)
target_link_libraries(DotExampleTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotExampleTests PRIVATE
    DOT_EXAMPLE_PROJECT="${CMAKE_BINARY_DIR}/examples/PrismRelay"
    DOT_EXAMPLE_SOURCE="${CMAKE_SOURCE_DIR}/examples/PrismRelay"
    DOT_LAB_SOURCE="${CMAKE_SOURCE_DIR}/examples/FeatureLab"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotExampleTests DotExamples)
dotDiscoverTests(DotExampleTests LABELS "integration;compiler" PROPERTIES TIMEOUT 60)

add_executable(DotFeatureLabTests main.cpp FeatureLabTests.cpp)
target_link_libraries(DotFeatureLabTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotFeatureLabTests PRIVATE
    DOT_FEATURE_LAB="${CMAKE_BINARY_DIR}/examples/FeatureLab"
    DOT_SCRIPT_SDK_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/scripts"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotFeatureLabTests DotFeatureLab)
dotDiscoverTests(DotFeatureLabTests LABELS "integration;compiler" PROPERTIES TIMEOUT 60)

add_executable(DotTemplateTests main.cpp TemplateTests.cpp)
target_link_libraries(DotTemplateTests PRIVATE DotEngine doctest::doctest)
target_compile_definitions(DotTemplateTests PRIVATE
    DOT_TEMPLATE_PROJECT="${CMAKE_BINARY_DIR}/examples/PrismRelay"
    DOT_TYPESCRIPT_COMPILER="${DOT_TYPESCRIPT_COMPILER}")
add_dependencies(DotTemplateTests DotExamples)
dotDiscoverTests(DotTemplateTests LABELS "integration;compiler" PROPERTIES TIMEOUT 30)

add_executable(DotProjectActionTests main.cpp ProjectActionTests.cpp)
target_link_libraries(DotProjectActionTests PRIVATE DotEngine doctest::doctest nlohmann_json::nlohmann_json)
target_compile_definitions(DotProjectActionTests PRIVATE DOT_EDITOR_RESOURCES="$<TARGET_FILE_DIR:DotEditor>/resources")
add_dependencies(DotProjectActionTests DotEditorResources)
dotDiscoverTests(DotProjectActionTests LABELS "integration;compiler" PROPERTIES TIMEOUT 30)

set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${dot_test_labels}")

add_test(NAME distribution.archive_excludes_finder_metadata
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/archive_acceptance"
        "-DARCHIVE_SCRIPT=${CMAKE_SOURCE_DIR}/cmake/ArchiveEditor.cmake"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/EditorArchiveTests.cmake")
set_tests_properties(distribution.archive_excludes_finder_metadata PROPERTIES
    LABELS "integration" TIMEOUT 15)

add_test(NAME project_copy.excludes_live_sessions_and_partial_resources
    COMMAND "${CMAKE_COMMAND}" "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/project_copy"
        "-DCOPY_SCRIPT=${CMAKE_SOURCE_DIR}/cmake/CopyProject.cmake"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/ProjectCopyTests.cmake")
set_tests_properties(project_copy.excludes_live_sessions_and_partial_resources PROPERTIES
    LABELS "integration" TIMEOUT 15)
