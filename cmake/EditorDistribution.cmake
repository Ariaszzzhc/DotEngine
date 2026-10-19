# Package the complete native editor without machine-specific toolchain paths.
# 打包完整原生编辑器，不记录特定机器的工具链路径。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(DotEditor DotRuntime PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
    target_link_options(DotEditor PRIVATE "LINKER:--disable-new-dtags")
    target_link_options(DotRuntime PRIVATE "LINKER:--disable-new-dtags")
endif()
install(TARGETS DotEditor DotRuntime RUNTIME DESTINATION . COMPONENT EditorDistribution)
if(WIN32)
    install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION . COMPONENT EditorDistribution)
    install(FILES "${DOT_DXC_DLL}" "${DOT_DXIL_DLL}" DESTINATION . COMPONENT EditorDistribution)
    install(FILES "${DOT_DXC_LICENSE}" DESTINATION licenses
        RENAME directx-dxc.txt COMPONENT EditorDistribution)
endif()
install(DIRECTORY "$<TARGET_FILE_DIR:DotEditor>/resources/" DESTINATION resources
    USE_SOURCE_PERMISSIONS COMPONENT EditorDistribution PATTERN ".DS_Store" EXCLUDE PATTERN ".dot-stage-*" EXCLUDE)
foreach(dot_notice IN LISTS dot_package_notices)
    get_filename_component(dot_notice_directory "${dot_notice}" DIRECTORY)
    get_filename_component(dot_notice_name "${dot_notice_directory}" NAME)
    install(FILES "${dot_notice}" DESTINATION licenses RENAME "${dot_notice_name}.txt"
        COMPONENT EditorDistribution)
endforeach()
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/licenses/" DESTINATION licenses COMPONENT EditorDistribution)
install(FILES "${DOT_EDITOR_FONT_LICENSE}" DESTINATION licenses
    RENAME dot-editor-font.txt COMPONENT EditorDistribution)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/examples/FeatureLab" DESTINATION examples
    COMPONENT EditorDistribution PATTERN ".DS_Store" EXCLUDE PATTERN ".dot-stage-*" EXCLUDE)
install(FILES "${CMAKE_SOURCE_DIR}/docs/Distribution.md" DESTINATION . RENAME README.md COMPONENT EditorDistribution)
configure_file("${CMAKE_CURRENT_LIST_DIR}/EditorInfo.plist.in"
    "${CMAKE_CURRENT_BINARY_DIR}/EditorInfo.plist" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/PrepareEditorDistribution.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PrepareEditorDistribution.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/PrepareEditorDistribution.cmake" COMPONENT EditorDistribution)
add_custom_target(DotEditorDistribution
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/distribution/DotEngine"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
        --component EditorDistribution --prefix "${CMAKE_BINARY_DIR}/distribution/DotEngine"
    DEPENDS DotEditor DotRuntime VERBATIM)

add_custom_target(DotEditorArchive
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIRECTORY=${CMAKE_BINARY_DIR}/distribution/DotEngine"
        "-DARCHIVE=${CMAKE_BINARY_DIR}/distribution/DotEngine-${DOT_KIT_PLATFORM}.zip"
        "-DSTAGE_DIRECTORY=${CMAKE_BINARY_DIR}/.dot-editor-archive-stage"
        -P "${CMAKE_CURRENT_LIST_DIR}/ArchiveEditor.cmake"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/distribution"
    DEPENDS DotEditorDistribution VERBATIM)
