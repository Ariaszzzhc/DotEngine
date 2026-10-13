# Authored examples remain ordinary native projects with the current script SDK.
# 编辑示例保持为普通原生项目，附带当前脚本 SDK。
add_custom_target(DotExamples ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/examples/PrismRelay/scripts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/examples/PrismRelay"
        "${CMAKE_BINARY_DIR}/examples/PrismRelay"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/assets/scripts"
        "${CMAKE_BINARY_DIR}/examples/PrismRelay/scripts"
    VERBATIM)

add_custom_target(DotFeatureLab ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/acceptance/FeatureLab/scripts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/acceptance/FeatureLab"
        "${CMAKE_BINARY_DIR}/acceptance/FeatureLab"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/assets/scripts"
        "${CMAKE_BINARY_DIR}/acceptance/FeatureLab/scripts"
    VERBATIM)
