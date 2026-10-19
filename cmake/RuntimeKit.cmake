# Build a native redistributable kit without altering caller toolchain settings.
# 构建原生可分发运行时目录，不改变调用者的工具链设置。
install(TARGETS DotGame RUNTIME DESTINATION . COMPONENT RuntimeKit)
install(FILES "${DOT_EDITOR_FONT}" DESTINATION . RENAME EngineSans.ttf COMPONENT RuntimeKit)
install(FILES "${DOT_EDITOR_FONT_LICENSE}" DESTINATION licenses
    RENAME dot-editor-font.txt COMPONENT RuntimeKit)
if(WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION . COMPONENT RuntimeKit)
    install(FILES "${DOT_DXC_DLL}" "${DOT_DXIL_DLL}" DESTINATION . COMPONENT RuntimeKit)
    install(FILES "${DOT_DXC_LICENSE}" DESTINATION licenses
        RENAME directx-dxc.txt COMPONENT RuntimeKit)
endif()
file(GLOB dot_package_notices
    "${CMAKE_SOURCE_DIR}/vendor/*/LICENSE*" "${CMAKE_SOURCE_DIR}/vendor/*/License*")
foreach(dot_notice IN LISTS dot_package_notices)
    if(IS_DIRECTORY "${dot_notice}")
        list(REMOVE_ITEM dot_package_notices "${dot_notice}")
    endif()
endforeach()
foreach(dot_notice IN LISTS dot_package_notices)
    get_filename_component(dot_notice_directory "${dot_notice}" DIRECTORY)
    get_filename_component(dot_notice_name "${dot_notice_directory}" NAME)
    install(FILES "${dot_notice}" DESTINATION "licenses" RENAME "${dot_notice_name}.txt"
        COMPONENT RuntimeKit)
endforeach()
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/licenses/" DESTINATION licenses COMPONENT RuntimeKit)
if(APPLE)
    set(DOT_KIT_PLATFORM "macos-arm64")
elseif(WIN32)
    set(DOT_KIT_PLATFORM "windows-x64")
else()
    set(DOT_KIT_PLATFORM "linux-x64")
endif()
set(DOT_KIT_SEARCH "${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES};${CMAKE_RUNTIME_OUTPUT_DIRECTORY};${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/FixNativeDependencies.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FixNativeDependencies.cmake" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/PrepareRuntimeKit.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PrepareRuntimeKit.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/PrepareRuntimeKit.cmake" COMPONENT RuntimeKit)
add_custom_target(DotRuntimeKit ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
        --component RuntimeKit --prefix "$<TARGET_FILE_DIR:DotGame>/resources/runtime-kit"
    DEPENDS DotGame VERBATIM)
