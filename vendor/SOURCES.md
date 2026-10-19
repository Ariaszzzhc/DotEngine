# Vendored source inputs / 第三方源码输入

The Git submodule commits and squash subtree commits are the authoritative pins
for full upstream trees. Compact copies below retain only the listed upstream
files; update them as a group from the stated revision and keep their license.
Git history records each local change after import.

Git submodule 提交与 squash subtree 提交是完整上游目录的固定版本。精简复制项只保留
下列版本中本项目需要的文件，升级时须同时更新源码、许可证与本清单。

| Directory | Upstream revision | Selected inputs |
| --- | --- | --- |
| `imgui` | `ocornut/imgui` `v1.92.9b-docking` | Core, demo, SDL3/WebGPU backends, `misc/cpp`, `misc/freetype`, internal headers, license |
| `stb` | `nothings/stb` `2c980bb59875b0d32144a71867fbdebb2f77cd20` | `stb_image.h`, `stb_image_write.h`, license |
| `cgltf` | `jkuhlmann/cgltf` `v1.15` | `cgltf.h`, license |
| `tinyobjloader` | `syoyo/tinyobjloader` `v2.0.0rc13` | `tiny_obj_loader.h`, `tiny_obj_loader.cc`, license |
| `xxhash` | `Cyan4973/xxHash` `v0.8.4` | `xxhash.h`, `xxhash.c`, `xxh3.h`, license |
| `nlohmann-json` | `nlohmann/json` `v3.12.0` | Upstream `single_include/nlohmann/json.hpp`, MIT license |
| `miniaudio` | `mackron/miniaudio` `0.11.25` | `miniaudio.h`, license, DotEngine `initialization-cleanup.patch` applied |

The editor font and host TypeScript compiler are pinned release artifacts
prepared at configure time; they are not part of the source vendor tree.
编辑器字体与宿主 TypeScript 编译器属于配置阶段准备的固定发布产物，不纳入源码 vendor。
