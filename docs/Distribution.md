# DotEngine editor distribution

The native distribution includes DotEditor, DotRuntime, the native player kit,
TypeScript compiler/standard libraries, current script SDK, CJK font, templates
and third-party notices. The engine remains a static C++26 module library; no
public header installation is needed by the delivered editor or games.

On macOS, double-click DotEngine.app in the extracted directory. It is a native
application bundle and starts directly through Finder. Compiler, resources and
libraries are inside the bundle, so the application can also move independently.
Run DotEditor.exe on Windows or DotEditor on Linux. The native
project home creates Camera and cube, Prism Relay or Feature Lab, or opens an
existing project folder. Choose an existing parent directory and a new folder;
existing destinations are preserved. Play clones the authored scene. The Resources
page edits saved TypeScript source; hot reload retains compatible state. Export
includes the native game player and runtime libraries.

```text
DotEditor
DotEditor open <project-folder>
DotEditor project-action <editor-folder>/resources <request-file>
```

On macOS the CLI executable is `DotEngine.app/Contents/MacOS/DotEditor` and resources
are in `DotEngine.app/Contents/Resources/resources`. The bundle uses SDL's standard
[resource-directory resolution](https://wiki.libsdl.org/SDL3/SDL_GetBasePath).
The native distribution manifest records executable paths for automation.

The top-level `examples/PrismRelay` and `examples/FeatureLab` directories are complete
projects, including their versioned `scripts/dot` SDK. Open these project folders
directly, or create a writable copy using the home template choices. A project folder
contains `project.dotproject`; its parent `examples` directory is not itself a project.
Keep persistent authoring copies outside generated distribution/build directories.

The distribution can move independently of its original build directory. Resource
paths resolve through the application base directory, compiler invocation uses the delivered tool,
and non-system native libraries use relative references. Operating-system libraries
and graphics/audio drivers remain system requirements. Each archive targets its
build's native platform; Windows/Linux code is provided but not locally validated.

For builders:

```text
cmake --preset release
cmake --build --preset release --target DotEditorDistribution
cmake --build --preset release --target DotEditorArchive
```

Output is `build/release/distribution/DotEngine` and the platform-named zip beside
it. Debug offers the same targets for diagnostic acceptance. These outputs are
owned/generated and refreshed by those targets; keep authored projects elsewhere.
Do not edit the delivered templates in place: project-home creation copies them
into the new project and installs the current SDK.

macOS dependency relocation and ad-hoc signatures enable tested local execution.
Release signing/notarization or native installer publication require a separate
release workflow; no result is claimed for those processes. Caller compiler/linker
settings are preserved and only distributed copies are relocated/signed.

Acceptance copies the distribution to a new path containing spaces, starts the
native home (including macOS LaunchServices), opens/type-checks its complete example,
creates an example project with packaged resources, runs the real
compiler and editor preview/native runtime, exports a game, then deletes the project
and editor copy before running the delivered game. Local checks also inspect native
loader references and signatures. Validation covers arm64 macOS Debug and
Release/NDEBUG; it does not promise every driver or external filesystem behavior.
