# Dependency sources / 依赖来源

DotEngine builds its native libraries from fixed source revisions with the
compiler and linker selected by `CMakePresets.json`. CMake does not choose a
separate dependency toolchain. Configure does not use vcpkg.

DotEngine 使用固定版本的本地源码构建原生库；依赖与项目目标使用同一套预设工具链，
配置过程不使用 vcpkg。

| Source method | Dependencies |
| --- | --- |
| Fixed Git submodule | Dawn, SDL3, JoltPhysics, SPIRV-Tools |
| Squashed Git subtree | doctest, GLM, EnTT, FreeType, RecastNavigation, QuickJS-NG, Abseil, Brotli, BZip2, libpng, zlib, SPIRV-Headers, WebGPU-Headers, Vulkan-Headers, Vulkan-Utility-Libraries |
| Selected copied files | Dear ImGui, stb, cgltf, tinyobjloader, xxHash, nlohmann-json, miniaudio, Dawn's Python Jinja2 and MarkupSafe packages |

`vendor/SOURCES.md` records the original revisions and included files for
selected copies. Submodule gitlinks and subtree split metadata record full-tree
revisions. Keep all notices when refreshing either kind. The QuickJS-NG source
has one project patch for embedded-NUL module names; the miniaudio copy contains
one project cleanup patch for failed device initialization. These are the
project-maintained source changes. An upstream submodule requiring a permanent
source change must be replaced with a pinned project fork.

The Sarasa editor font, host TypeScript 7.0.2 compiler and Windows DXC are
release artifacts, fetched during first configure using fixed URLs and SHA-512
in `cmake/ReleaseAssets.cmake`. A fresh checkout therefore needs network access
to configure; previously populated build directories reuse their own content.
The selected host TypeScript archive includes its runtime library files.
Windows DXC includes the matching compiler DLLs and import library; the runtime
kit and editor distribution carry the DLLs and notice.

Use `git submodule update --init --depth 1` after cloning or switching to a
commit that changes submodule pins. Do not recurse into Dawn's upstream
submodules: this project pins the selected Dawn dependencies separately under
`vendor/`. The normal `debug`/`release` presets create
separate `build/vendor-debug` and `build/vendor-release` directories. Do not
copy old CMake caches or module artifacts into them. Windows additionally needs
the MSVC/Windows SDK environment; Linux needs development headers for its
selected X11/Wayland and other OS integration APIs. These are platform SDK
inputs, not vendored engine libraries. See [toolchains](Toolchains.md).

Source, package and platform changes require fresh Debug and Release builds,
CTest, an incremental rebuild, and the real editor/export/runtime workflow.
Only macOS arm64 has been run for this vendor migration so far; Windows and
Linux support still needs execution on those hosts.
