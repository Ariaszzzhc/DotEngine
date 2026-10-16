# Authored examples use engine-owned dot: modules without copied SDK sources.
# 编辑示例使用引擎拥有的 dot: 模块，不复制 SDK 源码。
# Build copies are disposable; replace them so removed source assets cannot linger.
# 构建示例副本可随时重建，整体替换以清除源码中已删除的资源。
add_custom_target(DotExamples ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/examples/PrismRelay"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/examples/PrismRelay/scripts"
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}/examples/PrismRelay"
        "-DDESTINATION_ROOT=${CMAKE_BINARY_DIR}/examples/PrismRelay"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyProject.cmake"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/examples/PrismRelay/scripts/dot"
    VERBATIM)

add_custom_target(DotFeatureLab ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/examples/FeatureLab"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/examples/FeatureLab/scripts"
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}/examples/FeatureLab"
        "-DDESTINATION_ROOT=${CMAKE_BINARY_DIR}/examples/FeatureLab"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyProject.cmake"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/examples/FeatureLab/scripts/dot"
    VERBATIM)
