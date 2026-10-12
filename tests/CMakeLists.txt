find_package(doctest CONFIG REQUIRED)
add_executable(DotTests
    main.cpp
    CoreTests.cpp
    EventTests.cpp
    FileTests.cpp
    InputTests.cpp
    PlatformTests.cpp
    ApplicationTests.cpp
    MathTests.cpp
    GeometryTests.cpp
    AssetTests.cpp
)
target_link_libraries(DotTests PRIVATE DotEngine doctest::doctest SDL3::SDL3 nlohmann_json::nlohmann_json)
include(doctest)
doctest_discover_tests(DotTests PROPERTIES TIMEOUT 15 LABELS "unit" ENVIRONMENT "SDL_VIDEODRIVER=dummy")

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

add_executable(DotGraphicsTests main.cpp GraphicsTests.cpp)
target_link_libraries(DotGraphicsTests PRIVATE DotEngine doctest::doctest)
doctest_discover_tests(DotGraphicsTests PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.gpu COMMAND DotRuntime --gpu-test)
set_tests_properties(runtime.gpu PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.mesh COMMAND DotRuntime --mesh-test)
set_tests_properties(runtime.mesh PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
add_test(NAME runtime.render COMMAND DotRuntime --render-smoke)
set_tests_properties(runtime.render PROPERTIES TIMEOUT 60 LABELS "native" RUN_SERIAL TRUE)
