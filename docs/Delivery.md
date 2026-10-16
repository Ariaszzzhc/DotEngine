# Current implementation and evidence

Engine development has resumed after cleanup. DotEngine has real
rendering and authoring foundations, but has not demonstrated large-game production
readiness or editor maturity comparable to the requested target.

| Implemented foundation | Current boundary |
| --- | --- |
| Static C++26 library, separate module interfaces/implementations, pinned native dependencies | Preserve caller compiler/linker environment; no installed public header tree |
| PBR texture/material slots, HDRI IBL, directional soft shadows, SSAO and HDR/tone mapping | Point/spot shadows, temporal quality, streaming and populated-scene performance remain incomplete |
| OBJ/glTF/GLB import, native resources, GPU skinning, animation cross-fades and isolated editor audition | Full production animation/asset tools remain incomplete |
| Rigid-body physics with linear CCD, contacts/queries, audio, saved character controllers and shared scene simulation/Inspector | Rigged mannequin/shoulder template is implemented; final realistic character art and scalability remain incomplete |
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

The latest full macOS arm64 suites pass Debug 1053/1053 (147.36s) and Release/NDEBUG
1053/1053 (20.08s), including 270 unit tests and 123 offscreen GPU regressions. These
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
character query identity/self-filtering. Virtual-only contact callbacks and populated-game/crowd scalability remain unfinished.


Solid dynamic bodies expose saved discrete/continuous collision detection through
native configuration, strict scene/component schemas, the Inspector, SDK authoring
and actual backend state reads. Continuous mode uses native shape casts and rejects
unsupported Static/Kinematic/sensor combinations. Feature Lab provides a thin wall
and resettable fast slider with an editable Discrete comparison. Native editor
agent operations verify both outcomes, invalid input preservation and undo back to
the clean authoring scene. This does not establish arbitrary angular CCD or
sensor-crossing detection, final game art or production performance.

Inspector labels now wrap beside controls or stack in narrow panels. Numeric vector
edits preserve untouched components; strict stage-inspector / Apply / Reset share
Document preparation and atomic primary transfers with the GUI. Actual editor agent
checks cover drafts, rejection, apply/history, independent text drafts and clean close.
Native layout and Chinese staged text are inspected at multiple widths/themes;
simulated Name-field click focus remains unconfirmed.

The live agent interface now drives bounded keyboard/button/text/pointer input
through ordinary Play frames with automatic release and session identity checks.
Prism Relay product checks verify movement, one-shot jumping during a hold,
cancellation, restart isolation and shoulder-camera rotation under actual native
capture. Native focus requests report observed focus. The temporary development
app must own its executable and use standard bundle resources for that focus check.
The simulated Name-click coordinate discrepancy remains a separate SDL/global-pointer
validation limitation; agent property operations remain verified.

Actual viewport PNG export now shares File-menu and strict agent operations, with
owned frame provenance and native save dialogs. Isolated animation audition uses
real native players without gameplay scripts, physics simulation or audio. The
editor verifies all seven Prism Relay clips, fixed paused poses, bind/seek/reverse
controls, source-safe exit and subsequent normal Play. Model framing includes real
posed subtree bounds. Keyframe/skeleton/transition authoring remains incomplete;
these workflows do not establish final realistic game art or populated-game performance.
