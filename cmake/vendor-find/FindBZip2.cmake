# Resolve FreeType's BZip2 feature to the pinned in-tree target.
# 将 FreeType 的 BZip2 功能指向仓库固定的源码 target。
set(BZIP2_FOUND TRUE)
set(BZIP2_VERSION "1.0.8")
set(BZIP2_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/vendor/bzip2")
set(BZIP2_INCLUDE_DIRS "${BZIP2_INCLUDE_DIR}")
set(BZIP2_LIBRARIES BZip2Vendor)
if(NOT TARGET BZip2::BZip2)
    add_library(BZip2::BZip2 ALIAS BZip2Vendor)
endif()
