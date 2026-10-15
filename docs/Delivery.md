# Current implementation and evidence

Engine development has resumed after cleanup. DotEngine has real
rendering and authoring foundations, but has not demonstrated large-game production
readiness or editor maturity comparable to the requested target.

| Implemented foundation | Current boundary |
| --- | --- |
| Static C++26 library, separate module interfaces/implementations, pinned native dependencies | Preserve caller compiler/linker environment; no installed public header tree |
| PBR texture/material slots, HDRI IBL, directional soft shadows, SSAO and HDR/tone mapping | Point/spot shadows, temporal quality, streaming and populated-scene performance remain incomplete |
| OBJ/glTF/GLB import, native resources, GPU skinning and animation cross-fades | Full production animation/asset tools remain incomplete |
| Rigid-body physics, contacts/queries, audio, saved character controllers and shared scene simulation/Inspector | Humanoid content and shoulder-camera integration remain incomplete |
| TypeScript/QuickJS-NG, hot reload, dot:engine/dot:host and extensionless project imports | Existing script contracts are implemented; new native capabilities still need explicit SDK exposure |
| Docked editor, scene/resources/materials, picking/gizmos/multiselection/history, isolated Play and native export | Mature content-production workflows and scalability remain incomplete |
| CLI/strict JSON/live mailbox agent control and external source editing | Full AI control remains a product requirement for every current/future operation |
| Editable Feature Lab and Prism Relay under examples/ | Prism Relay still retains an intermediate collection startup; the non-shooting shoulder-view template is unfinished |

## Cleanup and verification scope

The cleanup removes stage-specific runtime/editor acceptance executables, automatic
desktop-window tests and the one-off Prism Relay mesh authoring script. Existing
native game assets remain authored project inputs. Feature Lab source, generated
copies, project-home templates and distribution inputs consistently use examples/.

Unit tests never initialize windows/devices. The automatic suite has no desktop
window launches; enduring offscreen GPU regressions remain integration checks.
Package relocation/script/animation behavior is verified through headless game
sessions. Real editor/standalone visual workflows use product agent operations,
with computer use for human-style interaction and unexpected UI failures.
See [testing](Testing.md) and [example workflow validation](Acceptance.md).

The current macOS arm64 suite passes Debug 994/994 (135.18s) and Release/NDEBUG
994/994 (19.08s), including 257 unit tests and 123 offscreen GPU regressions. These
counts describe verification scope, not overall production readiness. A separate
product check uses actual editor agent operations to save/reload character
scripts during Play, observe native movement/jumping, restore source, retain motion,
land and export a data package without changing scene authoring. The editor exits
successfully. One hot-reload frame reports about 200 ms dropped time; compilation
is synchronous and background compilation remains unfinished.
Details and historical results are recorded in [Development.md](Development.md).

Only arm64 macOS has been executed locally. Windows/Linux have implementation
support without a local execution claim. Portable packaging remains an explicit
product capability; no new interim archive is required by this cleanup. Release
identity signing/notarization and installer publication have not been completed.

The chronological development log retains historical evidence. It must not be read
as a current list of tools or an assertion that all original requirements are done.
The full [goal boundaries](EngineRoadmap.md) remain open.
