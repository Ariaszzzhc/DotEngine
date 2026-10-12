---
name: engine-validation
description: Validate DotEngine changes and staged engine releases using CMake, CTest, sanitizers, and executable acceptance cases.
---

Use the existing LLVM environment without overriding system settings. Configure
and build with `cmake --preset debug`, `cmake --build --preset debug`, then run
`ctest --preset debug`. Repeat meaningful release checks with the release presets;
use the sanitize presets for resource-lifetime or memory changes.

Review changed modules for error propagation, resource release after failed
initialization, event lifetime, frame timing, and operating-system guards. Verify
formatting using `.clang-format`. Run feature acceptance scenes when available;
unit tests alone do not establish GPU, audio-device, or application correctness.

Inspect the final macOS executable with `otool -L` and inspect its rpaths when
linkage changes. Record the tested platform and build configuration. Do not label
other platforms as tested. Keep generated builds and test scratch data out of Git.

