# Resolve FreeType/libpng's package query to the pinned in-tree target.
# 将 FreeType/libpng 的包查询指向仓库固定的源码 target。
set(ZLIB_FOUND TRUE)
set(ZLIB_VERSION "1.3.2")
set(ZLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendor/zlib")
set(ZLIB_INCLUDE_DIRS "${ZLIB_INCLUDE_DIR};${CMAKE_BINARY_DIR}/vendor/zlib")
set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_LIBRARIES zlibstatic)
if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
