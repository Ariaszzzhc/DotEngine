# Resolve FreeType's PNG feature to the pinned in-tree target.
# 将 FreeType 的 PNG 功能指向仓库固定的源码 target。
set(PNG_FOUND TRUE)
set(PNG_VERSION_STRING "1.6.59")
set(PNG_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendor/libpng")
set(PNG_INCLUDE_DIRS "${PNG_INCLUDE_DIR};${CMAKE_BINARY_DIR}/vendor/libpng")
set(PNG_LIBRARY png_static)
set(PNG_LIBRARIES png_static)
if(NOT TARGET PNG::PNG)
    add_library(PNG::PNG ALIAS png_static)
endif()
