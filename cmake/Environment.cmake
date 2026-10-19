include_guard(GLOBAL)

# Select one LLVM installation for C and C++; locate the platform's lld driver.
# 为 C 与 C++ 选择同一套 LLVM 安装，并定位平台对应的 lld 驱动。
if(DEFINED ENV{DOT_LLVM_ROOT} AND NOT "$ENV{DOT_LLVM_ROOT}" STREQUAL "")
    set(dot_llvm_bin "$ENV{DOT_LLVM_ROOT}/bin")
    find_program(dot_clang NAMES clang PATHS "${dot_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
    find_program(dot_clangxx NAMES clang++ PATHS "${dot_llvm_bin}" NO_DEFAULT_PATH REQUIRED)
else()
    find_program(dot_clang NAMES clang REQUIRED)
    find_program(dot_clangxx NAMES clang++ REQUIRED)
endif()
get_filename_component(dot_clang_bin "${dot_clang}" REALPATH)
get_filename_component(dot_clangxx_bin "${dot_clangxx}" REALPATH)
get_filename_component(dot_llvm_bin "${dot_clangxx_bin}" DIRECTORY)
get_filename_component(dot_llvm_root "${dot_llvm_bin}/.." ABSOLUTE)
get_filename_component(dot_clang_directory "${dot_clang_bin}" DIRECTORY)
if(NOT dot_clang_directory STREQUAL dot_llvm_bin)
    message(FATAL_ERROR "clang and clang++ must come from the same LLVM installation")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    # The clang++ executable uses the MSVC-compatible driver for SDK/STL and /MD.
    # clang++ 可执行文件以兼容 MSVC 的驱动模式读取 SDK/STL，并使用 /MD。
    set(CMAKE_C_COMPILER "${dot_clang};--driver-mode=cl")
    set(CMAKE_CXX_COMPILER "${dot_clangxx};--driver-mode=cl")
    set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
    set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set(dot_lld_name lld-link)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_C_COMPILER "${dot_clang}")
    set(CMAKE_CXX_COMPILER "${dot_clangxx}")
    set(CMAKE_OBJC_COMPILER "${dot_clang}")
    set(CMAKE_OBJCXX_COMPILER "${dot_clangxx}")
    set(dot_lld_name ld64.lld)
    # Current ld64.lld cannot synthesize Clang's newer Objective-C class stubs.
    # 当前 ld64.lld 无法生成 Clang 新版 Objective-C 类消息存根。
    set(dot_objc_stub_flags
        "-fno-objc-msgsend-selector-stubs -fno-objc-msgsend-class-selector-stubs")
    string(APPEND CMAKE_OBJC_FLAGS_INIT " ${dot_objc_stub_flags}")
    string(APPEND CMAKE_OBJCXX_FLAGS_INIT " ${dot_objc_stub_flags}")
    # Dawn compiles .mm sources as CXX; plain C++ ignores these ObjC-only flags.
    # Dawn 将 .mm 按 CXX 编译；普通 C++ 源文件会忽略这些仅限 ObjC 的标志。
    string(APPEND CMAKE_CXX_FLAGS_INIT
        " -Qunused-arguments ${dot_objc_stub_flags}")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_COMPILER "${dot_clang}")
    set(CMAKE_CXX_COMPILER "${dot_clangxx}")
    set(dot_lld_name ld.lld)
else()
    message(FATAL_ERROR "Unsupported LLVM host platform: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

find_program(dot_lld NAMES "${dot_lld_name}" HINTS "${dot_llvm_bin}" REQUIRED)
set(CMAKE_LINKER "${dot_lld}")
set(CMAKE_LINKER_TYPE LLD)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    # Require the headers and all three runtime libraries from this LLVM root.
    # 头文件和三个运行库必须来自同一 LLVM 根目录。
    find_path(dot_libcxx_headers NAMES __config
        PATHS "${dot_llvm_root}/include/c++/v1" NO_DEFAULT_PATH REQUIRED)
    set(dot_runtime_paths "${dot_llvm_root}/lib/c++" "${dot_llvm_root}/lib/unwind"
        "${dot_llvm_root}/lib" "${dot_llvm_root}/lib/x86_64-linux-gnu")
    find_library(dot_libcxx NAMES c++ PATHS ${dot_runtime_paths} NO_DEFAULT_PATH REQUIRED)
    find_library(dot_libcxxabi NAMES c++abi PATHS ${dot_runtime_paths} NO_DEFAULT_PATH REQUIRED)
    find_library(dot_libunwind NAMES unwind PATHS ${dot_runtime_paths} NO_DEFAULT_PATH REQUIRED)
    get_filename_component(dot_libcxx_directory "${dot_libcxx}" DIRECTORY)
    get_filename_component(dot_libcxxabi_directory "${dot_libcxxabi}" DIRECTORY)
    get_filename_component(dot_unwind_directory "${dot_libunwind}" DIRECTORY)
    string(APPEND CMAKE_CXX_FLAGS_INIT
        " -stdlib=libc++ -nostdinc++ -isystem \"${dot_libcxx_headers}\"")
    foreach(dot_kind IN ITEMS EXE SHARED MODULE)
        string(APPEND CMAKE_${dot_kind}_LINKER_FLAGS_INIT
            " -L\"${dot_libcxx_directory}\" -L\"${dot_libcxxabi_directory}\" -L\"${dot_unwind_directory}\" -lc++abi -lunwind")
    endforeach()
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(CMAKE_BUILD_RPATH
            "${dot_libcxx_directory};${dot_libcxxabi_directory};${dot_unwind_directory}")
    endif()
endif()

# CMake reads CFLAGS/CXXFLAGS/LDFLAGS itself, but does not read CPPFLAGS.
# CMake 自动读取 CFLAGS/CXXFLAGS/LDFLAGS，但不读取 CPPFLAGS。
if(DEFINED ENV{CPPFLAGS})
    string(APPEND CMAKE_C_FLAGS_INIT " $ENV{CPPFLAGS}")
    string(APPEND CMAKE_CXX_FLAGS_INIT " $ENV{CPPFLAGS}")
endif()
