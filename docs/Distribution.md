# DotEngine editor distribution

The native distribution includes DotEditor, DotRuntime, the native player kit,
TypeScript compiler/standard libraries, embedded engine script modules, CJK font, templates
and third-party notices. The engine remains a static C++26 module library; no
public header installation is needed by the delivered editor or games.

On macOS, double-click DotEngine.app in the extracted directory. It is a native
application bundle and starts directly through Finder. Compiler, resources and
libraries are inside the bundle, so the application can also move independently.
Run DotEditor.exe on Windows or DotEditor on Linux. The native
project home creates Camera and cube or Feature Lab, or opens an
existing project folder. Choose an existing parent directory and a new folder;
existing destinations are preserved. Play clones the authored scene. The Resources
page opens TypeScript in the system default application; hot reload retains compatible state. Export
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

The top-level `examples/FeatureLab` directory is a complete
project that imports the embedded `dot:engine` module. Open this project folder
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

Output is `build/vendor-release/distribution/DotEngine` and the platform-named zip beside
it. Debug offers the same targets for diagnosis. These outputs are
owned/generated and refreshed by those targets; keep authored projects elsewhere.
Do not edit the delivered templates in place: project-home creation copies them
into the new project; engine modules remain owned by the engine binaries.

macOS dependency relocation and ad-hoc signatures enable tested local execution.
Release signing/notarization or native installer publication require a separate
release workflow; no result is claimed for those processes. Caller compiler/linker
settings are preserved and only distributed copies are relocated/signed.

Distribution verification uses the actual editor home, project creation, Play and
export operations through the product's agent interface. Native picker, launch and
visual behavior can be checked with computer use. Relocation, loader references and
signatures require actual checks of the generated distribution. Historical results
are recorded in Development.md; no fresh distribution was requested by the cleanup.
