---
name: engine-validation
description: Validate DotEngine changes using CMake, CTest, and actual editor/example workflows.
---

Use the existing LLVM environment without overriding system settings. Configure
and build with `cmake --preset debug`, `cmake --build --preset debug`, then run
`ctest --preset debug`. Repeat meaningful release checks with the release presets;
check resource lifetime with allocation counters and repeated initialization tests.

Review changed modules for error propagation, resource release after failed
initialization, event lifetime, frame timing, and operating-system guards. Verify
formatting using `.clang-format`. Unit tests must not initialize windows or devices;
integration tests must not open desktop windows. Keep focused offscreen rendering
regressions for stable contracts. Validate integrated workflows through the real
editor's CLI/JSON agent interface. Use computer use to supplement this with
human-style interaction and unexpected visual/UI failures, not dedicated triangle/
cube demonstration executables. Unit tests alone do not establish application correctness.

Inspect the final macOS executable with `otool -L` and inspect its rpaths when
linkage changes. Record the tested platform and build configuration. Do not label
other platforms as tested. Keep generated builds and test scratch data out of Git.

