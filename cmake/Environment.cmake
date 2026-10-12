include_guard(GLOBAL)

# Resolve compiler names using the caller's PATH. 使用调用方 PATH 解析编译器名称。
foreach(language IN ITEMS C CXX)
    if(NOT DEFINED CMAKE_${language}_COMPILER)
        if(language STREQUAL "C")
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

# CMake consumes CFLAGS/CXXFLAGS itself, but does not consume CPPFLAGS.
# CMake 自动读取 CFLAGS/CXXFLAGS，但不自动读取 CPPFLAGS。
if(DEFINED ENV{CPPFLAGS})
    string(APPEND CMAKE_C_FLAGS_INIT " $ENV{CPPFLAGS}")
    string(APPEND CMAKE_CXX_FLAGS_INIT " $ENV{CPPFLAGS}")
endif()
