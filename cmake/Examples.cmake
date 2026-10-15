# Authored examples use engine-owned dot: modules without copied SDK sources.
# 编辑示例使用引擎拥有的 dot: 模块，不复制 SDK 源码。
add_custom_target(DotExamples ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/examples/PrismRelay/scripts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/examples/PrismRelay"
        "${CMAKE_BINARY_DIR}/examples/PrismRelay"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/examples/PrismRelay/scripts/dot"
    VERBATIM)

add_custom_target(DotFeatureLab ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/examples/FeatureLab/scripts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/examples/FeatureLab"
        "${CMAKE_BINARY_DIR}/examples/FeatureLab"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/examples/FeatureLab/scripts/dot"
    VERBATIM)
