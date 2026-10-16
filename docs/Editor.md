# Editor documents and automation

The editor currently provides native scene authoring through a shared document
service, JSON commands and a native project editing window. Runtime play uses a
separate GameSession scene; the Document service opens no audio, physics, compiler
or window backend. Workspace adds window/GPU/GUI ownership around that document.

`Dot.Editor.Document` opens the configured project startup scene with fresh scope,
owns bounded history and exposes read-only scene access. Each changed edit,
undo and redo prepares a validated scene before replacing the stable scene object.
Borrowed scene addresses survive document moves; entity handles must be reacquired
after scope replacement. All operations require the creating thread.

Document revisions are nonzero decimal strings in JSON, as are entity values and
scene scopes. Every mutation requires the current revision; stale requests fail
before effects. Inspect returns the revision, scope, selected value, next available
entity value (null on exhaustion), dirty flag, undo/redo availability, history entry/
byte totals and the complete version-1 native scene. Responses also include ok=true
and an array of surviving newly created values. Owned snapshots may be freely edited
without modifying the document.

## Requests

```json
{"command":"inspect"}
```

```json
{"command":"select","revision":"1","entity":"2"}
```

Selection accepts null to clear it and does not create an undo step or dirty the
scene. A changed selection advances the revision; repeated identical selection
preserves it. Mutation requests for undo, redo and save have only command and
revision fields. Saving is explicit. Failed validation/history preparation/saves
preserve the current scene, selection, history and revision.

```json
{
  "command":"edit",
  "revision":"2",
  "operations":[
    {"op":"name","entity":"2","name":"Player"},
    {"op":"transform","entity":"2","value":{
      "position":[1.5,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]
    }}
  ]
}
```

Edit operations use strict fields:

| op | Additional fields | Effect |
| --- | --- | --- |
| create | name | Allocate an entity with default transform and no components |
| remove | entity | Remove the entity and its complete subtree |
| name | entity, name | Rename an entity |
| scene-name | name | Rename the scene |
| parent | entity, parent | Keep local transform; null unparents |
| transform | entity, value | Replace the complete native transform |
| component | entity, component, value | Replace/remove one native component |

Components are camera, mesh, rigidBody, character, audioSource, audioListener,
script, material, light, animation and skin.
Values use the same schema as native scene files; null removes the component.
Script authoring describes a source/class/properties/enabled attachment and does
not execute code. Asset existence belongs to runtime bindings, so documents may
author valid paths to resources that have not yet been created.

Character controllers share the scene codec's complete capsule/movement/contact
properties. The Inspector uses the same defaults and component transaction as agent
edits. Rigid body and character controller are mutually exclusive; removal/addition
can be applied in one document transaction. Physics binds them only when Play or
an exported game initializes. Invalid world tilt/scale is an explicit binding
error. Scene parent/pose edits remain ordinary authoring operations; property-only
reconfiguration preserves runtime motion. See [physics](Physics.md).

Rigid-body Collision detection uses the `discrete`/`continuous` selector and the
same required `collisionDetection` field in scene/agent component values. Continuous
requires solid Dynamic motion. Invalid Static/Kinematic/sensor combinations preserve
the document and remain editable drafts. Native Play and exports consume the saved
mode; changing it while stopped is ordinary history-aware component authoring.

A batch has 1..256 operations within 1 MiB of UTF-8 JSON, with maximum depth 64.
Unknown/missing/duplicate fields, malformed/canonical-ID violations and invalid
native component/transform/hierarchy values fail. Validation checks the final
scene, allowing a batch to switch primary cameras/listeners without exposing an
intermediate invalid state. Create operations obey the native 10000-entity capacity;
parents must exist at the point they are referenced. Successful batches form one
undo step. No-op batches preserve revision, scope and history, including redo.

Published entity values are never reused within a document, including after undo
and new branches. Failed/no-op candidates publish no values. Undo/redo restore
selection from the target entry and always obtain fresh scene scopes. Removing a
selected subtree clears selection; undo restores it in the new scope. Editing after
undo drops redo history. Default limits retain 128 entries within 256 MiB; configurable
bounds are 1..1024 entries and 1 byte..512 MiB, including the current entry. Older
entries are evicted as needed. A scene larger than the byte budget is rejected.
History shares immutable snapshots instead of copying their payloads per edit.
The saved baseline may retain one extra snapshot up to the native 64 MiB limit.

Dirty state compares the current canonical authoring scene with the saved baseline,
so undo can return to a clean state and undo after saving can make it dirty again.
Save checks on-disk metadata and the semantic startup-scene baseline before atomic
replacement. External changes report a conflict and preserve unsaved edits. This
check is not an OS compare-and-swap; clients must still serialize filesystem changes.
If response allocation fails after a successful mutation, the error states that
the document committed; retrying with an old revision will fail.

## Command-line authoring

```text
DotEditor document <project-root> <requests-json-file>
```

The request file is a single request or an array of 1..256 requests, within 1 MiB/
depth 64. The command owns one in-memory document for the entire sequence and
emits one JSON line per request. Requests commit independently; a failed request
emits ok=false with contextual code/message, exits one and stops subsequent
requests. Earlier successful requests, including explicit saves, remain effective.
All-success sequences exit zero. There is no implicit save; unsaved changes disappear
when this command's document is destroyed. Initial revision is 1.

This sequence edits and explicitly saves:

```json
[
  {"command":"edit","revision":"1","operations":[
    {"op":"name","entity":"2","name":"Player"}
  ]},
  {"command":"save","revision":"2"}
]
```

The native [GUI rendering layer](Gui.md) provides persistent GPU previews and
isolated ImGui/SDL3/WebGPU contexts.

## Native workspace

```text
DotEditor open <project-root> [frames]
DotEditor send <control-directory> <request-id> <request-file>
```

`open` opens the project startup scene, a native window and the packaged Sarasa UI SC Regular font (with Inter Western glyphs). A zero/omitted frame budget runs until closed; a positive budget is useful
for diagnostics. It prints and flushes one JSON line containing `ok` and an absolute
`control` path. Window dimensions start at 1280x800 with minimum 800x480.

The scene tree selects entities; the inspector edits names, local transforms,
parents and native components. Script properties use strict JSON object
input, including arrays/null/nested values. Add/remove and property changes are
buffered until Apply changes or Save; Reset discards that buffer. Applying the
inspector produces one native document history entry. Selecting another entity,
undo/redo or automation mutations are rejected while inspector changes remain
pending. Save applies the inspector first. A primary camera switch clears the
previous primary in the same transaction. Component attachments describe resource
paths and do not implicitly generate assets/scripts.

The toolbar provides Save, Undo, Redo, New entity and Delete; Cmd+S/Cmd+Z/
Cmd+Shift+Z are used on macOS, Ctrl equivalents on Windows/Linux. Delete is guarded
while text input is active. Native close/quit checks document dirtiness, pending inspector changes and
resource file drafts, opening Save and close / Discard / Cancel confirmation.
The viewport defaults to an independent editor camera in a reusable GPU target.
Editor navigation never adds a camera to the game scene. Explicit Game view and
Play use the actual game primary camera; missing camera/assets display errors and
can be recovered by editing/undo. Native renderer faults stop the loop. The toolbar now includes Play/Stop (F5); project
resource authoring and external source opening are provided through Resources. Export
publishes standalone native games or data packages through the shared command below.

`send` reads one <=1 MiB strict UTF-8 JSON object and atomically publishes a request.
It returns `queued:true` and an absolute `response` path, **not** applied success.
Read that JSON file to observe the actual outcome. The record contains the original
request string in `source` (null for unreadable/non-UTF-8 input) and the native
result/error in `response`. Document
commands are the same inspect/edit/select/undo/redo/save protocol above. Inspect
also returns `workspace` with running/close/inspector status, buffered entity fields,
properties text and validation status. `stage-inspector` takes exactly `command`,
`revision`, `entity` and `value`. `entity` must be the current selected decimal ID;
`value` is its complete native entity object from the inspected scene (including
null components). It validates the final scene before replacing the pending draft,
without changing document revision, dirty state or history. Staging can replace the
current entity draft; stale revisions, pending environment/AO drafts, an open material
editor, Play and active transform gestures reject it without mutation. Separate text-file
drafts can coexist with entity drafts, matching the GUI. Unknown/missing fields and invalid components
are errors. Choosing a primary camera/listener prepares demotion of the previous
owner in the same transaction.

`apply-inspector` and `reset-inspector` require a current canonical decimal `revision`,
share the GUI implementation and return an updated inspection. Apply commits one
history step; Reset restores canonical fields and script properties text immediately.
The native Inspector keeps wrapped labels beside controls, stacks them in narrow
panels and edits numeric vector elements independently without narrowing other values.
Close uses this strict request without a revision:

```json
{"command":"close","discard":false}
```

Dirty close returns `confirmation:true` while keeping the application running;
clients can save then close, or explicitly close with `discard:true`. Applying
pending inspector changes is a separate explicit automation command.

Request IDs are 1..64 ASCII letters/digits/_/-. Identical-content retries reuse
one receipt without replay; different content for the same ID fails. Callers
serialize submissions and filesystem changes. Requests commit independently in
lexical ID order, at most sixteen per frame; at most 256 may be pending and 4096
IDs are retained per session. Use sequential submissions/current returned revisions
for dependent changes. A response failure after possible effects permanently faults
mailbox/frame processing without replay; the owner may still inspect/save the
Document directly. Readiness metadata identifies the owning session, not a process
liveness/heartbeat guarantee. No hostile filesystem-race isolation or crash durability
is promised. The private control directory is removed with Workspace ownership;
it is temporary session data, not project authoring content.

## Game preview

Play clones the current validated Document scene, including unsaved edits, into a
fresh scope and starts real TypeScript scripts, Jolt physics and native audio.
No authoring file is saved. Pending inspector changes must be applied or reset
first. Play/Stop and all close actions share the automation dispatcher. Scene
editing, selection, history and saving are locked while the game is running or
faulted; stopping returns to the authoring camera preview without changing the
Document's revision, dirty state, selection or undo history.

```json
{"command":"play","revision":"4"}
```

```json
{"command":"stop-play","revision":"4"}
```

These requests require the current authoring revision and return updated inspect
responses. Repeated play while active fails; stopping is idempotent. F5 toggles the
same operation. Inspect's `workspace.play` contains idle/running/faulted/stopped
state, frame/fixed-step counters, latest dropped time, input capture, errors and
at most 64 historical diagnostics. `workspace.message` exposes current GUI action
errors. `inspect-play` (only a command field) returns the actual game scene and
play status, including the retained final scene after stop.

Simulation owns independent fixed timing at the project's fixedHz and caps
catch-up work at eight steps per frame by default. Dropped fixed time is reported;
variable script/audio updates occur once per frame with delta capped at one second.
Native configuration may choose 1..64 steps. Scripts hot reload from project files
when configured; failed source keeps the old generation running and reports the
error. Ordinary component errors isolate that component and remain visible.
Realm/native game failures stop game advancement and stay observable in the editor;
Stop releases services and permits authoring/restarting. The editor frame itself
may succeed while displaying a faulted game; that does not claim game execution
succeeded. Stop executes script hooks and releases native audio/physics/script
ownership, including after game faults. Editor closure, frame failure and ownership
destruction also stop active games; destructor diagnostics go to stderr.

```json
{"command":"capture-input","capture":true}
```

The Game input checkbox uses this same request. Capture starts disabled; it routes
native keyboard/text to the game, leaving editor shortcuts inactive except F5.
Pointer coordinates are relative to the viewport in logical pixels. A running game
can request relative mouse mode with `input.capturePointer(true)`. It takes effect
only while Game input is enabled and the editor window is focused. Relative mode
hides/confines the cursor and supports continuous motion beyond window edges;
pointer buttons/motion/wheel are isolated from editor widgets. Escape, focus loss,
Stop or closing releases capture. Focus loss disables Game input until explicitly
enabled again. Outside-viewport clicks release ordinary absolute input capture.
While relative mode is active, SDL text/IME input is suspended so the current
input method cannot consume gameplay keys. Releasing relative mode restores text
input for editor fields; the operating system's selected input method is unchanged.
Released capture publishes released edges from the last game input, then
empty frames. Capture does not pause simulation/audio. No pause or single-step
control is provided by this stage. F5 and Escape are reserved editor controls.

`workspace.play.pointerCaptureRequested` reports the running script request;
`pointerCaptured` reports the last successfully applied native mode. A command
response can precede the next native frame that applies its request. The script's
`input.pointerCaptured` acknowledges the state published with that frame. These
fields also make the real pointer workflow inspectable by AI agents.
`textInputActive` reports the actual native text/IME mode; a failed native query
reports null with `inputModeError` instead of inventing a successful state.

```json
{"command":"save-close","revision":"4"}
```

```json
{"command":"cancel-close","revision":"4"}
```

These requests share the confirmation dialog's Save and close / Cancel operations.
Save and close first stops Play, applies the inspector, saves authoring and closes
only after success. Failure retains the editor and unsaved document; completed
stop/apply effects are not rolled back. Cancel clears the close confirmation.
Discard uses the existing close request with discard=true. Close cleanup errors
are returned by frame/run operations rather than substituted with successful exit.

Headless tests cover scene cloning, timing/input bounds, hot reload, script errors
and resource lifetime. Validate the actual workspace through its agent interface:
inspect authoring, start Play, inspect game state, stop and confirm authoring
isolation. Use computer use for F5/Escape, focus, pointer capture and visual behavior
when needed. Validation is on arm64 macOS.

## Agent gameplay input

Live Play inspection exposes a canonical decimal `play.session` identity. It changes
on each successful Play start even when the authoring revision stays unchanged.
A bounded request drives the ordinary InputState -> script -> native simulation
path; it does not change the scene directly or advance time outside the native loop.

```json
{"command":"input-play","session":"1","value":{"frames":30,"keys":["W"],"buttons":[],"pointer":[0,0],"pointerDelta":[0,0],"wheel":[0,0],"text":""}}
```

All fields are required. `value` is strict UTF-8 JSON within 64 KiB/depth 8. `frames`
is an integer in 1..600 and counts actual Play frames, rather than a duration or
fixed physics steps. Names match `dot:engine` input names exactly; unknown/duplicate
names and reserved editor keys Escape/F5 are rejected. Position and per-frame delta
are finite within +/-1e6, wheel within +/-1000, and text is UTF-8 within 16 KiB without
NUL. Pointer/wheel deltas repeat per held frame; text and press edges publish once.
The first input compares held keys/buttons against the last published frame. One
extra frame releases held keys/buttons and clears motion/wheel/text. A request is
accepted asynchronously; inspect the runtime scene and `play.agentInput` to verify
actual effects. `active`, `remaining` and `published` report its progression; the
published count includes its release frame. `play.inputSource` reports the latest
prepared input frame's `agent`, `native` or `none` source.

Only one request may be active, including its pending release frame. Cancel uses
exactly `command` and the current `session`:

```json
{"command":"cancel-play-input","session":"1"}
```

The GUI Release control button and Escape use this same cancellation mechanism.
Cancellation before first publication does not fabricate unseen presses/releases.
Stop, Play failure and destruction discard the controller; stale session identities
cannot inject into or cancel a restarted game. Pending close rejects new requests.

Agent input temporarily owns gameplay publication, independently of OS focus or
IME. It does not inject into the editor GUI or modify authoring history.
`focus-window` (only `command`) requests activation of the visible, non-minimized
editor through SDL; `inspect.workspace.focused` reports actual focus, which may
arrive asynchronously or be refused by the window manager. Native
input capture resumes afterward when enabled. Pointer capture remains an actual
frontend acknowledgement: an input request never claims OS pointer capture. Use the
existing `capture-input` operation when native relative capture is needed; focus loss
still releases that native mode. Agent deltas can be inspected without capture, but
scripts requiring acknowledged capture (such as the shoulder camera) obey that flag.

## Resources and external source editing

The Resources tab lists assets and the configured script directory in sorted
project-relative order. Select a text resource, then use Open externally or double
click it. New script creates Behavior with a dot:engine import and opens the new
file externally. The Script inspector also opens its authored source. All paths
use the same resource metadata, exact spelling, symlink and readonly checks as
agent file operations. Protected scripts/dot resources cannot be opened for
external editing. An invalid source encoding can still be opened to repair it.

The built-in code page and source-edit widget are removed. The 3D viewport remains
visible. Files open through the system's default file application using an encoded
file URI and [SDL's native launch interface](https://wiki.libsdl.org/SDL3/SDL_OpenURL).
Errors remain visible. Acceptance means the launch request succeeded; the engine
owns no external application and does not promise it loaded the document. Configure
an OS file association for .ts/.mts to choose a source editor. No universal default
application API promises line/column navigation, so diagnostic buttons open the
file while displaying its exact source position. Native hosts may supply an owned
WorkspaceConfig.openExternal launch callback; empty uses the system service.

Check scripts compiles saved files with the real pinned compiler and static linker.
Bounded positional compiler errors map from captured src paths back to actual
project sources, with filename/line/column buttons; nonpositional errors remain raw
context. The resources response exposes scripts.message and scripts.diagnostics.
Compilation errors are never replaced by diagnostic-parser success. External .ts/
.mts saves feed existing Play hot reload; invalid candidates retain old execution.

Agent open-text/replace-text/save-text/close-text operations retain bounded drafts,
identities/revisions, exact disk conflict checks and protected-path enforcement.
Resource controls show the agent buffer status and allow explicit save/close/discard,
without a source editor. Dirty agent drafts must be saved/discarded before Check,
Play or external opening. Scene shortcuts now always address the scene; agent file
saves are explicit. OBJ imports/agent saves invalidate the mesh cache; automatic
external asset watching is not implemented.

The same operations are available through live workspace requests:

```json
{"command":"open-external","path":"scripts/mover.ts"}
```

Success returns kind=external-file, accepted=true, the project-relative path and
encoded file URI. This action preserves the agent buffer and scene, releases game
input capture, and never saves source. Unknown fields, unsafe/missing/protected
resources, pending agent drafts, unavailable handlers and launch exceptions fail.

```json
{"command":"resources"}
```

This returns entries with path, byte-count string, directory/text/binary type and
readOnly, plus the current file or null. File state includes path/text, a canonical
nonzero buffer identity and revision, dirty and readOnly. Opening or reopening
increments buffer identity, so requests from an older buffer cannot modify the
new one even if its local revision is 1.

```json
{"command":"create-directory","path":"scripts/controllers"}
```

```json
{"command":"create-script","path":"scripts/controllers/player.ts"}
```

```json
{"command":"create-text","path":"assets/notes.txt","text":"Owned project notes"}
```

```json
{"command":"import-resource","path":"assets/model.obj","source":"/absolute/source/model.obj"}
```

Directories are created one level at a time; parent directories must exist. Existing
files/directories are never replaced by create/import. These operations write
immediately and return the refreshed catalog; if refresh fails after writing, the
error explicitly reports that creation committed.

```json
{"command":"open-text","path":"scripts/controllers/player.ts","discard":false}
```

Opening another/reopening file rejects pending changes unless discard=true is
explicit. Unsupported/binary files cannot be opened as text. Supported extensions
are .ts/.mts/.json/.md/.txt/.obj/.wgsl. Script templates require .ts/.mts rather than
declaration files, and must be inside the configured script tree. There is no
implicit code attachment or source compilation.

```json
{"command":"replace-text","buffer":"1","revision":"1","text":"export const score=1;"}
```

```json
{"command":"save-text","buffer":"1","revision":"2"}
```

```json
{"command":"close-text","buffer":"1","revision":"3","discard":false}
```

Replace changes only the in-memory draft; save compares exact on-disk baseline
bytes before atomic replacement. Stale identity/revision, invalid UTF-8/NUL/size,
protected resource writes, external changes, missing files and changed project metadata fail
without losing the draft. No-op replace/save preserve revision. Close rejects
unsaved state unless explicitly discarded. Normal window close checks file drafts;
Save and close saves both the scene and resource file, retaining the editor if a
conflict occurs. Completed stop/apply/file saves are not rolled back if a later
scene save fails. Destruction never implicitly saves drafts.

```json
{"command":"check-scripts"}
```

This actually compiles/types-checks the on-disk project using the packaged native
compiler and releases the temporary generation after obtaining its result. It
returns actual sourceFiles on success and contextual diagnostics on failure.
Opening/editing/saving a file clears earlier check messages, avoiding a stale
success label for changed source.

Resource operations are restricted to assets and the configured scripts subtree.
The protected `scripts/dot` path also checks filesystem directory identity. Paths follow shared
lexical [portable naming rules](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file),
including Windows superscript device names, and reject symlinks/special files and
ASCII case aliases/collisions. The limits are 1 MiB per editable text, 64 MiB per
binary import, 8192 catalog/lookup entries and 64 levels by default. Native callers
may lower limits; bounds are enforced before writes. Lexical validity does not
promise every filesystem accepts the path. Callers serialize project filesystem
changes: the baseline check plus replacement is not an OS compare-and-swap, hostile
race isolation or crash-durability promise.

Focused resource tests cover actual Unicode files/catalog, protected-path enforcement,
revision/no-op/conflict behavior, binary source preservation/import boundaries,
invalid paths/text/metadata, symlinks/depth/capacity, moves/thread ownership and
shared descriptor lifetime. Validate buffer identity, dirty close/recovery and
live source-save behavior through the actual workspace agent operations. Local validation remains
arm64 macOS; Windows/Linux code is not locally validated.

## Exporting the current scene

The toolbar's **Export game** modal uses the strict `export` or `export-data` command to
publish the current scene and saved resources without saving the authoring scene.
It requires stopped Play, the current revision and no pending inspector/file
drafts. See [game data export](Export.md) for the format, CLI, limits and ownership.
Native export includes the player and runtime libraries; data-only export remains
available through the checkbox.

## Starting and creating projects

The launcher separates Open/Create pages with aligned fields, persistent form values,
real template descriptions and a destination preview. SDL native folder pickers
support opening an existing project and choosing a creation parent. Cancellation
preserves the form; selection/errors use the shared validated project operations.

DotEditor now opens a [native project home](ProjectHome.md) by default, with open
and validated creation from the starter project, Prism Relay and Feature Lab.
The GUI, direct JSON command and live mailbox share the same project operations.

## Material and direct light authoring

The inspector can add/remove `material` and `light` components. Material binds a
portable project-root `.dotmaterial` path; light kind uses directional/point/spot
choices with color, intensity/range, cone angles and enabled fields. Transform
controls move/rotate the light's local -Z ray. All edits, history, save and Play
use the same scene validation as strict agent `component` operations. For example:

```json
{"op":"component","entity":"2","component":"material","value":{"asset":"assets/copper.dotmaterial"}}
```

Materials are owned resource files with the strict schema in [Rendering](Rendering.md).
Resource creation/import/save supports `.dotmaterial`; successful saves refresh the
shared preview cache. Preview/Play/export load actual referenced data and expose
failures. Feature Lab is an editable project demonstrating persisted PBR and normal
mapping. This stage provides basic inspectors and resource/agent authoring; dedicated
material panels, visual light tools and the requested mature editor layout remain
in development. External source editing and the engine script namespace are
implemented; the remaining editor layout/tooling stays part of the roadmap.

## Scene environments

Use the Environment tab to choose a project-relative HDRI (or use the selected
`.hdr` resource), adjust intensity/yaw/background and choose a processing quality.
Apply commits one undoable scene transaction; Reset discards the pending draft.
Save preserves the settings. Pending drafts protect Save/Play/export/close, and
Save and close applies the draft before saving. Environment and entity-inspector
drafts cannot be silently committed over one another. Preview, Play and exported
players use the authored scene environment through the shared renderer/cache.

Agent operations use the same document transaction:

```json
{
  "command": "edit",
  "revision": "1",
  "operations": [{
    "op": "environment",
    "value": {
      "asset": "assets/studio.hdr",
      "intensity": 1,
      "rotationDegrees": 0,
      "background": true,
      "radianceSize": 256,
      "diffuseSize": 32,
      "specularSize": 128,
      "brdfSize": 128,
      "samples": 256
    }
  }]
}
```

A null value removes the environment. Live workspaces also accept `stage-environment`
with revision/value, followed by `apply-environment` or `reset-environment` with
revision. `inspect` reports pendingEnvironment/environmentDraft. `show-pane` with
pane `scene`, `viewport`, `inspector`, `environment`, `rendering`, `resources` or `console` controls graphical navigation without a
scene edit/revision. Resource failures appear with their paths; no substitute HDRI
is supplied. Higher quality increases first-bake work; background import/bake jobs
and advanced environment controls are still planned.

## Directional shadows

The Light inspector's Cast directional shadows control enables saved cascade
settings. Expand shadows to choose resolution/count/coverage/splitting/bias and
filter radius; the integer controls preserve the strict work types. Point/spot
shadow requests are rejected. Mesh inspectors expose independent casting and
receiving flags. Apply/undo/save, agent component edits, Play and exported players
share the scene configuration. One enabled directional shadow source is supported
per frame; additional sources report a contextual error rather than losing shadows.
Advanced light gizmos, projection previews and point/spot shadows remain planned.

## Saved ambient occlusion

The Rendering pane enables geometric SSAO and edits its radius/surface bias in
meters, strength and Low/Balanced/High search/filter quality. Low uses 4 directions,
2 steps and 1 filter pixel; Balanced 8/4/2; High 16/8/4. Quality preserves radius,
bias and strength. AO shades diffuse HDR environment light; it leaves direct light,
specular, emission and unlit colors unchanged. Add an HDRI through Environment to
light diffuse surfaces. Normal maps alone do not create geometric occlusion.

Apply rendering commits one undoable scene transaction; Reset AO draft discards
unapplied edits. Save, Play, export, undo/redo and other mutations reject pending
AO drafts. Native close prompts and Save and close applies the draft before saving;
Discard leaves the saved scene untouched. Environment, inspector and AO drafts are
exclusive. Reopened scenes, Play's isolated copy and relocated exported players
use the same saved settings; Feature Lab includes an authored configuration.

```json
{
  "command": "stage-occlusion",
  "revision": "1",
  "value": {
    "radius": 0.5,
    "bias": 0.02,
    "strength": 1,
    "directions": 8,
    "steps": 4,
    "filterRadius": 2
  }
}
```

`apply-occlusion` and `reset-occlusion` require the current revision. Stage accepts
null to disable AO. `inspect` exposes pendingOcclusion/occlusionDraft, and
`show-pane` accepts rendering without a revision. The direct document `edit`
operation is `{ "op": "occlusion", "value": ... }` with the same settings/null
and validation. Work values must be integers; unknown fields and unsupported
ranges fail before history or drafts change. AO is optional scene data. Temporal reprojection, multi-layer/offscreen geometry and
populated-scene quality/cost work remain planned.

## Independent scene navigation and selection

The session-owned editor view provides perspective/orthographic browsing, RMB plus
WASD/QE flight (Shift accelerates), Alt+LMB orbit, MMB pan, wheel dolly and F to
frame the selected entity. Frame scene unions current visible mesh bounds, including
parent transforms and reflection; nonmesh entities focus on their world position.
Focus adjusts clip planes for larger bounds. Editor state is never written to the
scene or its undo history. Play temporarily uses the game camera and Stop restores
the editor view. An empty scene is editable without inventing a game camera.

Left-click selection rasterizes covered mesh fragments at the displayed pixel with
integer draw identifiers, depth testing and the same physical backface policy as
the surface pass. Full entity IDs remain on the CPU, including values above 2^53.
Background clicks clear selection. This is synchronous single-pixel GPU readback;
marquee selection, advanced manipulators and asynchronous readback are
not implemented. Drafts and stale scene/view revisions reject selection rather than
picking an image that no longer describes the document. Native drag ownership starts
inside the viewport and ends on release, Escape or focus loss.

Agent commands share these implementations:

- `set-view`: `mode` is `editor` or `game`. Optional editor `camera` contains
  `position`, `pivot`, `yaw`, `pitch`, `speed`, `orthographic`,
  `orthographicHeight`, `nearPlane`, `farPlane`; degrees and meters are explicit.
- `navigate-view`: `action` is `turn` (`delta`, `orbit`), `move` (`direction`,
  `seconds`, `fast`), `pan` (`delta`) or `dolly` (`steps`).
- `focus-view`: current `revision`, `entity` as a decimal string or null to frame
  all visible meshes.
- `pick-view`: current `revision`, top-left normalized `uv` in [0,1). A successful
  preview of the same view/document is required first.

The visual direction is recorded in [EditorDesign.md](EditorDesign.md). Its reference
art is a design input; the scene and controls must always reflect engine state.

## Docked workspace and themes

Real ImGui docked windows replace fixed child regions: Hierarchy at left, independent
Scene/Game viewport in the center, Inspector/Environment/Rendering tabs at right and
Assets/Console below left/center. Tabs and splitters can move, resize and float inside the
application window. Window focuses each pane and resets the initial 18/60/22 and
25-percent bottom proportions. No additional native windows or terminal are created
by docking. File/Edit/Build menus invoke the same existing scene/export operations.
Assets Actions contains actual resource/import/script-check and agent-buffer actions.
Hierarchy and asset filters search names/paths, retaining matching hierarchy ancestors;
ASCII search ignores case, other UTF-8 text matches exactly. Layout/theme are currently
session state; saving the scene never writes them into game data.

- `set-theme`: `theme` is `dark` or `light`. Palette changes preserve metrics, viewport
  pixels, camera, selection and document history, including during Play.
- `filter-panel`: `panel` is `hierarchy` or `resources`; `query` is UTF-8, at most
  128 bytes, without embedded NUL. GUI search uses this same validation.
- `reset-layout`: no additional fields. Applied in the next GUI frame; a pending
  docking command must finish first.
- `dock-panel`: `panel`/`target` are `hierarchy`, `viewport`, `inspector`, `environment`,
  `rendering`, `resources` or `console`; target must currently be docked. `placement` is `tab`,
  `left`, `right`, `up` or `down`, with `ratio` in [0.1,0.9] (unused for tabbing).
  Returns `queued:true`. `inspect.workspace.layout` reports pending, command generation,
  error and actual per-panel logical geometry/visibility/focus/docking after the owning frame.
  A target moved away before execution reports an error instead of false completion.

These commands require no scene revision and remain available with authoring drafts
or during Play; they do not mutate the scene. `inspect.workspace.theme` and `filters`
report session state. Sarasa UI SC Regular supplies Inter Western/CJK mixed text from
one pinned packaged font, rendered at a compact logical size with native DPI handling.
Gizmos, thumbnails, cross-session profiles and detached native panels remain planned.

Console presents actual workspace/viewport errors, Play failure context and compiler
locations that open externally. Runtime diagnostics remain available after Stop and
do not shrink the scene image. Unsaved status includes scene, inspector/environment/
AO and agent text drafts. Editor Undo/Redo poll command shortcuts across docked
windows while text input retains its local undo. Newly appearing panels do not steal
requested focus. `inspect.workspace.displayed` reports hierarchy IDs and asset paths
actually drawn in the last frame; searches expand matching ancestors. View status
also reports text-input capture for input diagnostics.

Viewport rendering resolves after GUI layout and before GUI submission using the
current physical dimensions, with explicit absence for recoverable scene failures.
GUI build-time renderer reentry remains forbidden. This keeps initialization, docking,
resize, undo and Play/Stop pixels consistent with the displayed widget.

## Transform manipulators / 变换操纵器

Scene view offers Select/Move/Rotate/Scale with world/local coordinates. Move snaps
in meters, Rotate in degrees and Scale in factor increments; zero is continuous.
Select an entity and drag an axis, rotation ring or scale endpoint; the live GPU preview uses
a disposable scene clone. Release commits one history entry; Escape or focus loss
cancels. The Inspector/document stays unchanged during dragging. Undo/Redo restores
both authored data and actual scene pixels. Local axes include the selected hierarchy's
rotation/reflection; world displacement is converted through the parent inverse.
Unrepresentable parent-local shear or singular matrices fail without substituting an
approximate transform. Behind-camera and near-parallel axis handles are omitted.
Scene 视图提供选择/移动、世界/局部坐标轴及米单位吸附（零表示连续）。拖动修改独立
预览场景，松开后提交一条历史记录；Esc 或失焦取消，拖动期间不更改 Inspector/文档。
父局部错切或奇异矩阵无损失败，不用近似变换掩盖错误。

The UI and agent use the same gesture implementation:

```json
{"command":"set-tool","tool":"move","space":"world","snap":0.5}
{"command":"begin-transform","revision":"3","axis":0,"uv":[0.5,0.5]}
{"command":"update-transform","revision":"3","uv":[0.7,0.5]}
{"command":"finish-transform","revision":"3","commit":true}
```

`axis` is integer 0/1/2 (X/Y/Z); UV is top-left [0,1]. Begin requires a selected
entity, current rendered Scene view and no pending authoring/text draft or navigation.
Update is absolute relative to the start sample, avoiding cumulative snapping drift.
Cancel uses `commit:false`. An active gesture locks other document/view/layout/tool
operations; inspect, updates, finish, close, theme and filtering remain available.
Agent gestures stay active across frames until finish; native gestures additionally
consume pointer release. Closing asks about the pending draft before discarding it.
`workspace.tool` reports configuration, active entity, errors and actual projected
handle segments. `workspace.view.source` distinguishes document/transform/Play previews;
changed/begun/cancelled drafts invalidate readiness until GPU drawing completes.
Rotation rays intersect the operation frame's canonical plane, unwrapping by the
shortest arc between samples (up to 100 turns). Local rotation uses parent-world *
local T*R without entity scale; the rendered ring and inverse-ray sampling use the
same affine frame, including parent stretch/reflection. Local rotation preserves
original position and scale; local scale preserves position and rotation. World
operations preserve the world pivot and reject unrepresentable parent-local shear.
Scale factors are .001..1000, preserving existing reflection without crossing zero.
Snap limits: meters 0..1000, degrees 0..360, factor increments 0..10.
`workspace.tool.value` reports the last successfully applied axis distance/angle/factor
while active, and null otherwise. Agent input is isolated from incidental native
mouse motion; both sources use the same preview/commit implementation.
Plane movement, uniform-scale handles, multi-selection and richer snapping remain
unimplemented. These tools do not establish mature-editor or large-game completion.

Rendering displays the last successful mesh submission's input/visible/culled counts
and shadow/SSAO geometry draws; `workspace.view.draws` exposes the same report.
The report is null before a successful mesh call, remains unchanged after failure
and describes the last submission while view.ready independently marks freshness.
Counts reflect conservative GPU draw submissions rather than visible-pixel estimates
or measured frame rate. Picking keeps its own report and never overwrites mesh data.

Mesh submission inspection now adds `colorDraws`, `shadowDraws` and `occlusionDraws`
for actual GPU commands, distinct from `input`/`visible`/`culled`/`shadow`/`occlusion`
instance quantities. Rendering labels the two rows explicitly. Per-instance variation
is preserved after batching and does not change scene/Inspector authoring contracts.

The Rendering pane also shows actual CPU visibility tests: BVH nodes, world AABBs,
exact local-box tests and unindexed homogeneous tests. `inspect.workspace.view.draws.spatial`
includes indexed/changed/refitted quantities and rebuild status for the last successful
mesh pass. Camera-only changes reuse bounds/tree topology. These counts exclude
linear input validation and bounds generation; they are not frame-time or FPS claims.
渲染面板显示真实空间分类次数，检查接口还报告更新/重建状态；计数不含线性输入
校验和边界生成，也不表示帧耗时或 FPS。

## Model import and instantiation

Assets Actions offers Import glTF / GLB model using the source file and a new
project `assets/...` folder. After import, select its `model.dotscene` and use Add
model to scene. Imported hierarchy, transforms, material references and editable
native files use the actual engine; insertion can be undone/redone once and saved.
The current scene's camera/environment/lighting remain authoritative.

```json
{"command":"import-model","source":"/absolute/model.glb","path":"assets/robot"}
{"command":"edit","revision":"3","operations":[{"op":"instantiate-model","path":"assets/robot/model.dotscene"}]}
```

Import optionally accepts an unsigned `scene` index 0..1023. It reports source scene,
mesh/material/image/node and animation counts, generated scene and native model
descriptor paths, and metadata warnings. TRS clips and node bindings survive import;
Add model to scene retains the animation player and remaps its node targets.
Inspector supports clip/clock/loop/auto-play settings; Play uses the same native
GameSession as exported games. TypeScript playback controls use the same players; timeline tools remain unfinished.
Import publishes reusable resources without dirtying the scene; instantiation needs the
current authoring revision. A failed catalog refresh after publication explicitly
reports that the bundle was committed. Existing destinations are never overwritten.
Native `.gltf`, `.dotmesh`, `.dotscene`, `.dotmodel` and `.dotanimation` text resources
can open in the external editor; internal text transport remains only an automation buffer.

Material coverage is persisted in `.dotmaterial` alphaMode (`opaque`, `mask`, `blend`)
and alphaCutoff fields. Imports retain glTF alpha semantics; referenced files open in
the external editor. Rendering and `workspace.view.draws` show actual `solid`, `blended`,
`solidDraws` and `blendedDraws` values from the completed GPU pass. Feature Lab includes
Glass and textured Cutout entities using the actual engine policies.

## Structured material authoring / 结构化材质编辑

Select a `.dotmaterial` asset in Assets, or use **Edit material asset** on an entity's
Material component, to open its Inspector. Assets / Actions / New material creates
a complete material at a new `assets/*.dotmaterial` path; existing files are never
replaced. The Inspector provides PBR/unlit shading, base RGBA/emission, metallic,
roughness, normal scale, occlusion strength, sidedness, Opaque/Mask/Blend and cutoff.
Five texture sections select project PNG/JPEG images and edit color space, filtering,
addressing, mipmaps and anisotropy. Data slots retain linear color space. Missing
images, unsupported normal data and exhausted budgets report actual preview errors.

Fields are an unsaved staging form and preview through the real SceneRenderer on
all scene objects referencing that shared asset. They do not write the asset or
change scene history. **Apply material** makes one material history step, avoiding
one undo entry per slider frame. **Reset fields** restores the current history value;
**Save material** applies staged fields and atomically saves. Material history keeps
128 states, retains exact original source bytes and survives Save. Undo after Save
can make the asset dirty again. Save rejects exact disk-baseline conflicts and
retains the draft for recovery. Close/discard, project close confirmation, save-close,
Play and export observe pending material fields/history. Material mutations are
blocked during Play; Play renders saved assets, and Stop restores authoring preview.
The toolbar/File/Edit save and undo/redo actions address the open material; scene
history remains separate. Script editing continues in the external default editor.
Material graphs remain unfinished; the asset browser below provides actual thumbnails and a studio sphere.

GUI and live workspace commands use the same codec, document and preview path:

```json
{"command":"create-material","path":"assets/surface.dotmaterial"}
{"command":"open-material","path":"assets/surface.dotmaterial","discard":false}
```

The response contains `material` with decimal-string `buffer` and `revision`, complete
`value`, staged `draft`, `dirty`, `canUndo` and `canRedo`. `resources` and `inspect` /
`workspace.material` expose the same state. `stage-material` takes `buffer`, `revision`
and a complete material `value` and previews it without advancing history.
`edit-material` takes those same fields and commits one history step. `reset-material`,
`undo-material`, `redo-material` and `save-material` require current buffer/revision;
`close-material` additionally requires `discard`. Opening a different material/text
buffer requires no pending resource changes or explicit discard and invalidates old
buffer identities. Invalid descriptions/unknown fields/stale requests preserve state.
Undo/redo require staged fields to be applied or reset first. Texture contents are
validated at actual preview/Play/export use, rather than silently substituted.

## Asset browser and isolated previews / 资产浏览与独立预览

Assets has grid/list modes, folder breadcrumbs and project-wide search when the
filter is nonempty. Empty search shows the current folder's immediate children.
Visible PNG/JPEG, DotMesh/OBJ and material tiles request actual previews; other file
types show their type labels. Mesh tiles frame real geometry with neutral PBR studio
shading; material tiles shade a UV sphere through the existing five-texture/coverage
pipeline. Source image aspect/alpha are retained. Missing/invalid sources show an
error, never a sample thumbnail. Click an asset for Inspector preview, click a
material for typed editing; text resources still open externally on double-click.

Material Inspector shows a 256px sphere for the current unsaved form even when no
scene entity uses that material. Previews use a separate offscreen renderer and
packaged CC0 Studio Small 09 HDRI; they do not modify scene camera, lighting, SSAO,
HDR targets, draw statistics or files. This is a static studio preview, not a
material graph or a replacement for judging the material in its scene lighting.

One owned background thread performs source decoding, HDRI loading/baking, isolated
GPU creation/rendering/readback and GPU destruction. The GUI submits owned requests
and polls up to eight completed images per frame, then uploads current results to
its own device. It does not wait for preview rendering. Same-slot requests coalesce;
material forms have priority over FIFO thumbnails. Ticket/form checks discard late
results after edits, refresh, cache eviction and close/reopen.

The worker holds at most 64 pending requests, one active request and eight completed
images (16..256px each, at most 2 MiB total completed pixels). Completion saturation
pauses starting more work. GUI caches at most 64 128px thumbnails plus one 256px
material image (each CPU/GPU image set at most 4.25 MiB); the isolated texture resolver
retains its existing 64 MiB budget. These limits do not include bounded source
image decode/transient rendering. Play pauses starting queued work; an already-active
operation may finish, and results are collected after Stop. Refresh invalidates all
tickets and requests provider recreation on its own thread, without a UI-thread join.
Initialization errors remain explicit until refresh; there is no sample fallback.

GUI texture uploads still occur on the main thread; this is not a zero-copy pipeline
or a general stall-free/FPS guarantee. Close/destruction cancels queued work and
joins the worker, allowing an active file/GPU call to finish safely. No worker is
detached or forcibly interrupted. Cached images remain immutable until save/import
or explicit refresh; visible rows are clipped. Scene/model/audio/HDR/script thumbnail
formats and asynchronous model import are separate unfinished work.

Workspace GUI and automation share these strict commands:

```json
{"command":"set-assets-view","mode":"grid"}
{"command":"set-asset-directory","path":"assets"}
{"command":"select-asset","path":"assets/copper.dotmaterial"}
{"command":"request-preview","path":"assets/normal.png"}
{"command":"preview-assets"}
{"command":"refresh-previews"}
{"command":"clear-asset-selection"}
```

`set-assets-view` accepts grid/list; an empty directory path selects the project
root. Asset selection/directory requests validate against the current catalog.
`request-preview` queues supported files and does not claim generation succeeded.
`preview-assets` and `inspect.workspace.previews` report pending count, processed jobs,
ready/error/warning states, current view/directory, device presence and material
preview readiness/pending/error matched to the current form. Worker state includes
active/paused/stopped, pending/completed and submitted/started/finished/discarded
counters. Dirty material forms must be saved/discarded before asset
selection changes. `captureAssetPreview` returns owned pixels only for completed,
current cached previews; stale/pending/failed requests return errors. Scene/model
hierarchy, HDR panorama, audio and script thumbnails are not implemented.

Hierarchy mouse selection is committed after the tree traversal releases its scene
snapshot references. `inspect.workspace.hierarchyRows` reports the actual rendered
row rectangles and expansion state by entity ID for native input automation.
Selecting a parent, child or the current entity does not mutate scene data or add
undo history; a changed selection advances the document revision.

## Scene multiselection and group edits

The document owns an ordered set of selected entities. `selection` contains unique
canonical ID strings; `selected` is its last member (the active entity), or null
for an empty set. Selection changes advance the revision without dirtying the
scene or adding undo entries. Scene edits/undo/redo retain surviving selections
and restore the selection stored with each history entry. Selection ID payloads
count toward the existing bounded history budget. Saved scene files contain no
editor selection, so opening a project starts with none.

Hierarchy plain clicks replace selection; Command-click on macOS / Ctrl-click on
Windows/Linux toggles a member. Shift-click selects the inclusive open/filtered
row range from the anchor; adding Command/Ctrl preserves the prior selection.
The clicked range endpoint becomes active, including when selecting backwards.
Expanding an arrow does not select an entity. Highlighting shows every member.
The Inspector states the selected count and edits only the active entity's
properties. Viewport picking and Focus currently act on a single active entity.
Move/Rotate/Scale gizmos transform the current selection around its active pivot.
Group framing, box selection and mixed-value bulk properties remain planned.

The toolbar Duplicate (Command/Ctrl+D) and Delete operations act on the whole
selection in one undo transaction. Descendant unions prevent parent/child overlap
from duplicating/removing a subtree twice. Duplication assigns new monotonic IDs,
remaps internal parents, retains external parents/native components/asset bindings,
adds bounded UTF-8 ` Copy` names and selects the mapped input entities in order.
Copies of cameras/audio listeners are non-primary; originals remain primary.
Script properties are opaque and are copied unchanged, without guessing that an
arbitrary property string/number refers to an entity. Unsupported final scene
states (such as exceeding the light/entity limit) fail transactionally.

Shared GUI/agent commands require the current canonical decimal revision:

```json
{"command":"select-many","revision":"7","entities":["2","5"]}
{"command":"select-hierarchy","revision":"8","entity":"9","mode":"range-add"}
{"command":"duplicate-selection","revision":"9"}
{"command":"delete-selection","revision":"10"}
```

`select-many` replaces the ordered set, accepting an empty array to clear it;
duplicates, stale/foreign/missing entities and more than 10000 handles fail.
`select-hierarchy` uses the current displayed hierarchy order and the modes
replace/toggle/range/range-add; unknown/hidden IDs or modes fail. Explicit document
selection/edit/undo/redo resets its anchor to the active entity. Raw `edit` batches
can use `duplicate` or `remove-many` operations with a nonempty `entities` array.
Group deletion/duplication rejects an empty selection. Scene draft/Play/gesture
guards apply to GUI and agent paths alike.

Group gestures freeze the selected IDs, active pivot/frame, root world poses and
external parent inverses at the start. Only topmost selected roots receive local
changes; selected/unselected descendants inherit once through the hierarchy.
World operations use world axes around the active origin. Group Local rotation/
scale use the active entity's frozen parent-world × local T×R frame, excluding
its own scale while retaining ancestor stretch/reflection. This is a shared group
frame, not independent rotations around each member's center.

All samples are absolute from the gesture start; position/orientation/scale do not
accumulate rounding across pointer events. Every root must have representable
parent-local TRS. Shear/singular/overflow/invalid-factor failures leave the entire
last valid draft unchanged. Returning to the origin restores original local values
exactly. Escape/focus loss/cancel discard the owned draft; release commits one
typed transform transaction and selection-aware undo entry. Preview and committed
rendering use the same scene path. Single-object local behavior remains unchanged.

`begin-transform`/`update-transform`/`finish-transform` control the same group gizmo
used by native mouse input. `inspect.workspace.tool` reports frozen `entities`,
topmost `roots`, active `entity` and `pivot: "active"` during a gesture. The typed
Document transform transaction supports up to 10000 current-scene handles without
the generic JSON edit operation-count/transport bound; existing scene/history
limits still apply. Median/individual-origin pivot modes and group framing remain
unimplemented.

Saved [skin bindings](Skinning.md) render automatically in preview and isolated Play.
The Skin Inspector chooses an existing .dotskin asset, toggles enabled state and
edits ordered live joint entities; a mesh is required. Applying uses ordinary native
component/history validation. Duplicate requires all joints in the selected subtree
union, remaps their IDs and rejects external references without partial edits.
Model-scene instantiation remaps the same references. Bounds/framing/picking use the
current deformed mesh. Skeleton visualization and a visual
animation timeline remain unfinished.

Move a character root that contains its joints, or edit the joint transforms, to
pose a skinned mesh. Its mesh-owner world transform cancels through the skin palette;
moving only that mesh entity does not move the joint-driven world geometry.

glTF/GLB import now creates these same native skin assets and scene components.
`import-model` reports generated `skins` and `skinnedMeshes`; instantiate the returned
scene to insert the complete joint hierarchy and animation bindings. Every copied
joint receives a fresh entity ID. Asset import does not silently insert a character
or enable different collision shapes. The [skin regressions](Skinning.md) check actual
import, Inspector, duplicate/undo, isolated Play and relocated native export.

Viewport inspection now includes a nullable `view.error` string with the latest
actual preview failure. `captureViewport` includes that context when no preview can
be captured; it does not report a successful frame for a failed asset load. A later
successful draw clears the error. A native missing-mesh/undo case checks diagnostic
visibility and recovery of the original pixels after the Docking layout settles.
