# Example projects

## Feature Lab

[Feature Lab](../examples/FeatureLab/README.md) is the editable engine capability
project under `examples/FeatureLab`. It exercises components, the runtime SDK,
native character motion and relative mouse look. It is intended for feature
validation, not as a game template or production-scale workload.

```text
cmake --preset debug
cmake --build --preset debug
build/vendor-debug/bin/DotEditor open build/vendor-debug/examples/FeatureLab
```

Generated copies under `build/vendor-<preset>/examples/` are replaced on builds. Use
project home to create a writable copy, or copy the generated folder before
persistent editing. The source project opens directly and resolves engine-owned
`dot:engine` imports without copied SDK sources. The editor's normal export
workflow can publish a standalone native game from an ordinary project.

Headless regressions check Feature Lab's saved content and scripts. Integrated
visual checks use the actual editor and its strict agent operations. Local
product evidence covers arm64 macOS only.
