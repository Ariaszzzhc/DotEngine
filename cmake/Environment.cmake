include_guard(GLOBAL)

# Resolve compiler names using the caller's PATH. 使用调用方 PATH 解析编译器名称。
foreach(language IN ITEMS C CXX)
    if(NOT DEFINED CMAKE_${language}_COMPILER)
        if(language STREQUAL "C" AND DEFINED ENV{CC})
            set(CMAKE_C_COMPILER "$ENV{CC}")
        elseif(language STREQUAL "CXX" AND DEFINED ENV{CXX})
            set(CMAKE_CXX_COMPILER "$ENV{CXX}")
        elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            set(CMAKE_${language}_COMPILER clang-cl)
        elseif(language STREQUAL "C")
            set(CMAKE_C_COMPILER clang)
        else()
            set(CMAKE_CXX_COMPILER clang++)
        endif()
    endif()
    if(DEFINED CMAKE_${language}_COMPILER AND NOT IS_ABSOLUTE "${CMAKE_${language}_COMPILER}")
        find_program(DOT_${language}_COMPILER NAMES "${CMAKE_${language}_COMPILER}" REQUIRED NO_CACHE)
        set(CMAKE_${language}_COMPILER "${DOT_${language}_COMPILER}" CACHE FILEPATH "Compiler" FORCE)
    endif()
endforeach()

# Preserve vcpkg's architecture, runtime, and per-configuration settings.
# 保留 vcpkg 的架构、运行库及构建配置设置。
if(VCPKG_TARGET_TRIPLET MATCHES "-osx")
    include("${CMAKE_CURRENT_LIST_DIR}/../external/vcpkg/scripts/toolchains/osx.cmake")
elseif(VCPKG_TARGET_TRIPLET MATCHES "-linux")
    include("${CMAKE_CURRENT_LIST_DIR}/../external/vcpkg/scripts/toolchains/linux.cmake")
elseif(VCPKG_TARGET_TRIPLET MATCHES "-windows")
    include("${CMAKE_CURRENT_LIST_DIR}/../external/vcpkg/scripts/toolchains/windows.cmake")
endif()

# CMake consumes CFLAGS/CXXFLAGS itself, but does not consume CPPFLAGS.
# CMake 自动读取 CFLAGS/CXXFLAGS，但不自动读取 CPPFLAGS。
if(DEFINED ENV{CPPFLAGS})
    string(APPEND CMAKE_C_FLAGS_INIT " $ENV{CPPFLAGS}")
    string(APPEND CMAKE_CXX_FLAGS_INIT " $ENV{CPPFLAGS}")
endif()
