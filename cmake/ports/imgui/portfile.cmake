# Reuse the checked-in baseline's source hashes/build recipe, while the overlay
# manifest prevents enabling unrelated Dawn defaults on Windows/Linux.
# 复用仓库固定 baseline 的源码哈希/构建配方；覆盖清单阻止隐式启用 Windows/Linux
# 上无关的 Dawn 默认后端。
include("${CMAKE_CURRENT_LIST_DIR}/../../../external/vcpkg/ports/imgui/portfile.cmake")
