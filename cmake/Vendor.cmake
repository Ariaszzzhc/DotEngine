# Build pinned source dependencies with the project's selected toolchain.
# 使用项目已选定的工具链构建固定版本的第三方源码。

set(dot_vendor_root "${CMAKE_SOURCE_DIR}/vendor")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build vendored libraries statically" FORCE)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${dot_vendor_root}/zlib" "${CMAKE_BINARY_DIR}/vendor/zlib" EXCLUDE_FROM_ALL)

add_library(BZip2Vendor STATIC
    "${dot_vendor_root}/bzip2/blocksort.c"
    "${dot_vendor_root}/bzip2/bzlib.c"
    "${dot_vendor_root}/bzip2/compress.c"
    "${dot_vendor_root}/bzip2/crctable.c"
    "${dot_vendor_root}/bzip2/decompress.c"
    "${dot_vendor_root}/bzip2/huffman.c"
    "${dot_vendor_root}/bzip2/randtable.c")
target_include_directories(BZip2Vendor PUBLIC "${dot_vendor_root}/bzip2")
add_library(BZip2::BZip2 ALIAS BZip2Vendor)

set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory("${dot_vendor_root}/brotli" "${CMAKE_BINARY_DIR}/vendor/brotli" EXCLUDE_FROM_ALL)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/vendor-find")
set(SKIP_INSTALL_ALL ON)
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
set(PNG_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
add_subdirectory("${dot_vendor_root}/libpng" "${CMAKE_BINARY_DIR}/vendor/libpng" EXCLUDE_FROM_ALL)

set(FT_REQUIRE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_REQUIRE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_REQUIRE_PNG ON CACHE BOOL "" FORCE)
set(FT_REQUIRE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(BROTLIDEC_INCLUDE_DIRS "${dot_vendor_root}/brotli/c/include")
set(BROTLIDEC_LIBRARIES brotlidec)
if(WIN32)
    set(BUILD_SHARED_LIBS ON)
endif()
add_subdirectory("${dot_vendor_root}/freetype" "${CMAKE_BINARY_DIR}/vendor/freetype" EXCLUDE_FROM_ALL)
add_library(Freetype::Freetype ALIAS freetype)
if(WIN32)
    unset(BUILD_SHARED_LIBS)
endif()
unset(SKIP_INSTALL_ALL)

if(WIN32)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${dot_vendor_root}/sdl3" "${CMAKE_BINARY_DIR}/vendor/sdl3" EXCLUDE_FROM_ALL)

set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_HAS_SANITIZER OFF CACHE BOOL "" FORCE)
add_subdirectory("${dot_vendor_root}/entt" "${CMAKE_BINARY_DIR}/vendor/entt" EXCLUDE_FROM_ALL)
add_subdirectory("${dot_vendor_root}/glm" "${CMAKE_BINARY_DIR}/vendor/glm" EXCLUDE_FROM_ALL)

set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "" FORCE)
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory("${dot_vendor_root}/recastnavigation"
    "${CMAKE_BINARY_DIR}/vendor/recastnavigation" EXCLUDE_FROM_ALL)
set_target_properties(Recast Detour DetourCrowd PROPERTIES SYSTEM TRUE)

set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
if(WIN32)
    set(JPH_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
else()
    set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
add_subdirectory("${dot_vendor_root}/joltphysics/Build"
    "${CMAKE_BINARY_DIR}/vendor/joltphysics" EXCLUDE_FROM_ALL)

set(DAWN_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(DAWN_SUPPORTS_CXX_MODULES FALSE CACHE BOOL "" FORCE)
if(WIN32)
    set(DAWN_BUILD_MONOLITHIC_LIBRARY SHARED CACHE STRING "" FORCE)
else()
    set(DAWN_BUILD_MONOLITHIC_LIBRARY STATIC CACHE STRING "" FORCE)
endif()
set(DAWN_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_PROTOBUF OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_CMD_TOOLS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_WGSL_READER ON CACHE BOOL "" FORCE)
set(TINT_BUILD_WGSL_WRITER ON CACHE BOOL "" FORCE)
set(TINT_BUILD_SPV_READER OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_SPV_WRITER OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_METAL OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_NULL ON CACHE BOOL "" FORCE)
set(DAWN_USE_GLFW OFF CACHE BOOL "" FORCE)
set(DAWN_USE_X11 OFF CACHE BOOL "" FORCE)
set(DAWN_USE_WAYLAND OFF CACHE BOOL "" FORCE)
if(APPLE)
    set(DAWN_ENABLE_METAL ON CACHE BOOL "" FORCE)
elseif(WIN32)
    set(DAWN_ENABLE_D3D12 ON CACHE BOOL "" FORCE)
else()
    set(DAWN_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
    set(DAWN_USE_X11 ON CACHE BOOL "" FORCE)
    set(DAWN_USE_WAYLAND ON CACHE BOOL "" FORCE)
    set(TINT_BUILD_SPV_READER ON CACHE BOOL "" FORCE)
    set(TINT_BUILD_SPV_WRITER ON CACHE BOOL "" FORCE)
endif()
set(DAWN_ABSEIL_DIR "${dot_vendor_root}/abseil")
set(DAWN_JINJA2_DIR "${dot_vendor_root}/jinja2")
set(DAWN_MARKUPSAFE_DIR "${dot_vendor_root}/markupsafe")
set(DAWN_SPIRV_HEADERS_DIR "${dot_vendor_root}/spirv-headers")
set(DAWN_SPIRV_TOOLS_DIR "${dot_vendor_root}/spirv-tools")
set(DAWN_WEBGPU_HEADERS_DIR "${dot_vendor_root}/webgpu-headers")
set(DAWN_VULKAN_HEADERS_DIR "${dot_vendor_root}/vulkan-headers")
set(DAWN_VULKAN_UTILITY_LIBRARIES_DIR "${dot_vendor_root}/vulkan-utility-libraries")
if(WIN32)
    # Keep Dawn pristine while giving Tint its expected DXC header layout.
    # 保持 Dawn 原版源码，将 DXC 头文件摆在 Tint 预期的构建目录布局中。
    set(dot_dxc_header_root
        "${CMAKE_BINARY_DIR}/vendor/dawn-third-party/directx-shader-compiler/src/include/dxc")
    file(MAKE_DIRECTORY "${dot_dxc_header_root}")
    file(GLOB dot_dxc_headers "${DOT_DXC_INCLUDE}/*.h")
    file(COPY ${dot_dxc_headers} DESTINATION "${dot_dxc_header_root}")
    set(DAWN_THIRD_PARTY_DIR "${CMAKE_BINARY_DIR}/vendor/dawn-third-party")
    add_library(DotDxcompiler SHARED IMPORTED GLOBAL)
    set_target_properties(DotDxcompiler PROPERTIES
        IMPORTED_IMPLIB "${DOT_DXC_IMPLIB}" IMPORTED_LOCATION "${DOT_DXC_DLL}")
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(dot_dxc_runtime_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    else()
        set(dot_dxc_runtime_directory "${CMAKE_BINARY_DIR}")
    endif()
    file(MAKE_DIRECTORY "${dot_dxc_runtime_directory}")
    file(COPY_FILE "${DOT_DXC_DLL}" "${dot_dxc_runtime_directory}/dxcompiler.dll"
        ONLY_IF_DIFFERENT)
    file(COPY_FILE "${DOT_DXIL_DLL}" "${dot_dxc_runtime_directory}/dxil.dll"
        ONLY_IF_DIFFERENT)
endif()
add_subdirectory("${dot_vendor_root}/dawn" "${CMAKE_BINARY_DIR}/vendor/dawn" EXCLUDE_FROM_ALL)
if(WIN32)
    foreach(dot_dxc_target IN ITEMS dawn_native dawn_native_objects dawn_platform)
        target_compile_definitions(${dot_dxc_target} PRIVATE DAWN_USE_BUILT_DXC)
    endforeach()
    target_include_directories(dawn_native_objects PRIVATE "${DOT_DXC_INCLUDE}")
    target_link_libraries(dawn_native PRIVATE DotDxcompiler)
endif()

add_library(imgui STATIC
    "${dot_vendor_root}/imgui/imgui.cpp"
    "${dot_vendor_root}/imgui/imgui_demo.cpp"
    "${dot_vendor_root}/imgui/imgui_draw.cpp"
    "${dot_vendor_root}/imgui/imgui_tables.cpp"
    "${dot_vendor_root}/imgui/imgui_widgets.cpp"
    "${dot_vendor_root}/imgui/misc/cpp/imgui_stdlib.cpp"
    "${dot_vendor_root}/imgui/misc/freetype/imgui_freetype.cpp"
    "${dot_vendor_root}/imgui/backends/imgui_impl_sdl3.cpp"
    "${dot_vendor_root}/imgui/backends/imgui_impl_wgpu.cpp")
target_include_directories(imgui SYSTEM PUBLIC
    "${dot_vendor_root}/imgui" "${dot_vendor_root}/imgui/backends"
    "${dot_vendor_root}/imgui/misc/cpp" "${dot_vendor_root}/imgui/misc/freetype")
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE IMGUI_USE_WCHAR32)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
target_link_libraries(imgui PUBLIC SDL3::SDL3 dawn::webgpu_dawn Freetype::Freetype)
if(APPLE)
    set_source_files_properties("${dot_vendor_root}/imgui/backends/imgui_impl_wgpu.cpp"
        PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(imgui PUBLIC "-framework Foundation")
endif()
add_library(imgui::imgui ALIAS imgui)

add_library(tinyobjloader STATIC "${dot_vendor_root}/tinyobjloader/tiny_obj_loader.cc")
target_include_directories(tinyobjloader SYSTEM PUBLIC "${dot_vendor_root}/tinyobjloader")
add_library(tinyobjloader::tinyobjloader ALIAS tinyobjloader)

add_library(xxhash STATIC "${dot_vendor_root}/xxhash/xxhash.c")
target_include_directories(xxhash SYSTEM PUBLIC "${dot_vendor_root}/xxhash")
add_library(xxHash::xxhash ALIAS xxhash)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${dot_vendor_root}/nlohmann-json")
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)

set(QJS_BUILD_LIBC OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_STATIC OFF CACHE BOOL "" FORCE)
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${dot_vendor_root}/quickjs-ng" "${CMAKE_BINARY_DIR}/vendor/quickjs-ng" EXCLUDE_FROM_ALL)

if(BUILD_TESTING)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory("${dot_vendor_root}/doctest" "${CMAKE_BINARY_DIR}/vendor/doctest" EXCLUDE_FROM_ALL)
endif()

set(Stb_INCLUDE_DIR "${dot_vendor_root}/stb")
set(CGLTF_INCLUDE_DIR "${dot_vendor_root}/cgltf")
set(MINIAUDIO_INCLUDE_DIRS "${dot_vendor_root}/miniaudio")
