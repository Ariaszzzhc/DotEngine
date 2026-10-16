# Current implementation and evidence

Engine development has resumed after cleanup. DotEngine has real
rendering and authoring foundations, but has not demonstrated large-game production
readiness or editor maturity comparable to the requested target.

| Implemented foundation | Current boundary |
| --- | --- |
| Static C++26 library, separate module interfaces/implementations, pinned native dependencies | Preserve caller compiler/linker environment; no installed public header tree |
| PBR texture/material slots, HDRI IBL, directional soft shadows, SSAO and HDR/tone mapping | Point/spot shadows, temporal quality, streaming and populated-scene performance remain incomplete |
| OBJ/glTF/GLB import, native resources, GPU skinning and animation cross-fades | Full production animation/asset tools remain incomplete |
| Rigid-body physics, contacts/queries, audio, saved character controllers and shared scene simulation/Inspector | Rigged mannequin/shoulder template is implemented; final realistic character art and scalability remain incomplete |
| TypeScript/QuickJS-NG, hot reload, dot:engine/dot:host and extensionless project imports | Existing script contracts are implemented; new native capabilities still need explicit SDK exposure |
| Docked editor, scene/resources/materials, picking/gizmos/multiselection/history, isolated Play and native export | Mature content-production workflows and scalability remain incomplete |
| CLI/strict JSON/live mailbox agent control and external source editing | Full AI control remains a product requirement for every current/future operation |
| Editable Feature Lab and Prism Relay under examples/ | Prism Relay now uses a native rigged character and shoulder camera; final realistic art and performance remain unfinished |

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

The current macOS arm64 suite passes Debug 1022/1022 (147.56s) and Release/NDEBUG
1022/1022 (20.59s), including 258 unit tests and 123 offscreen GPU regressions. These
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

Native relative mouse capture is now available through dot:engine with distinct
script requests and frontend acknowledgements. Feature Lab includes M-toggle mouse
look. A separate actual editor session verifies native capture, pointer-driven
rotation, Escape and minimized-window release, unchanged authoring after Stop and
clean process exit. Headless tests cover VM guards, reload and input publication.
Prism Relay now integrates a real rigged mannequin and native sphere-swept shoulder camera.
The actual editor verifies W movement and Space jumping under the user's Chinese
input method: relative gameplay suspends SDL text input, and Escape/Stop restore it.
Native Play inspection exposes actual text mode. The stopped authoring scene stays
clean; normal close exits successfully. Finished realistic humanoid/suit art and
populated-game performance remain open.


Native characters now own broad-phase query/contact proxies. Typed collider identity
keeps character hits distinct from mutable rigid bodies; scene/SDK queries and native
sensor/rigid proxy events resolve the owning entity. Removal, teleport and mixed
replacement share event-capacity preflight, including full-capacity replacement.
Prism Relay excludes itself during camera sweeps, and Feature Lab demonstrates
character query identity/self-filtering. Per-body CCD controls, virtual-only contact
callbacks and populated-game/crowd scalability remain unfinished.
