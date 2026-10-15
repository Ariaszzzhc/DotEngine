# Project home

Starting DotEditor with no arguments opens the native project home. It opens an
existing project folder or creates a validated project from Empty project
(camera/cube/starter script), Prism Relay or Feature Lab. All three creation choices publish
one new folder under an existing parent, using the embedded engine modules and packaged compiler.
Existing directories/files/symlinks are preserved. Select a project to enter the
normal editor workspace; the home GUI/video resources release before that window
opens. An open/create failure stays visible in the home for correction.

The graphite launcher separates Open and Create pages with persistent form values.
Open offers a primary Browse action plus a pasted-folder alternative. Create aligns
project/folder names, offers parent-location browsing, explains each real template
and previews the destination. Its content scrolls at smaller window sizes; no
placeholder recent-project list is shown.

Both Browse actions use [SDL_ShowOpenFolderDialog](https://wiki.libsdl.org/SDL3/SDL_ShowOpenFolderDialog), attached to the current native
window. Selecting a project runs the same strict open validation as the command;
selecting a parent only fills Location. Cancellation preserves all form values.
Only one picker may be pending; form actions, command close and releaseWindow wait
for its completion. A callback-owned mailbox copies SDL's borrowed UTF-8 strings
before returning, retains no Home/window pointer, and safely delivers a result from
another thread. The GUI owner thread consumes it once. Callback failures and invalid
project folders remain visible for correction; no fake selection is substituted.
Full destruction while an OS picker is pending has no manual acceptance claim.

```text
DotEditor
DotEditor home [frames]
DotEditor project-action <resources-root> <request-file>
```

The optional unsigned frame budget is an acceptance/control option. Zero or
omission waits until selection or closure; malformed budgets fail before opening
native resources. The non-GUI project-action command uses the exact project
operation called by the native buttons and returns the normal project/scene JSON.

```json
{"command":"open","root":"/existing/project"}
```

```json
{"command":"create","parent":"/existing/output","folder":"MyGame","name":"My Game","template":"prism"}
```

Template IDs are blank, prism and lab. Open reads and validates without writing.
Creation copies rather than modifying the template. It
validates metadata/scene, compiles actual TypeScript and checks static resources
and compiled script setup before exclusive publication. It does not run arbitrary
gameplay hooks or promise future dynamic resource loads will succeed. Camera/cube
creation passes through the same staged validation rather than publishing first
and then reporting a compiler error.

Dot.Project.Templates exposes instantiateTemplate for native clients. Input changes
must be serialized. The copied source is limited to 8192 visited entries, 64 levels,
64 MiB/file and 512 MiB total. Portable paths,
ASCII aliases, symlinks/special files and private staging data are checked. On macOS,
ordinary .DS_Store files up to 64 KiB are ignored. Source metadata and bytes remain
unchanged; validation and copy failures remove only owned private staging. These
operations do not provide hostile-race isolation or crash durability guarantees.

## Automation and handoff

The home emits a JSON line containing kind=project-home and its temporary control
path, then flushes stdout. It accepts inspect/open/create/close requests through
the same request/response files as the workspace:

```text
DotEditor send <home-control> <id> <request-file>
```

Inspect returns the templates, running state, selected root, staged form,
dialog pending/target, visible control rectangles and last message. `browse-folder`
accepts `target: "project" | "parent"` and invokes the same native picker as the GUI.
`stage-form` accepts a complete `value` with root/parent/folder/name/template/page;
UTF-8/no-NUL/size/template/page checks precede assignment, while incomplete paths
may be staged for editing. Actual open/create retains full filesystem validation.
Close ends the selection loop when no native picker is pending. Request IDs use 1..64 ASCII letters/digits/_/- and may not be
reused for different content. At most 4096 request files and 256 pending requests
are allowed, with up to 16 processed per frame in lexical order. Successful
selection marks the home session selected, rejecting later submissions.

Home responses remain alive while the entered workspace owns the main flow.
After selection the editor prints another JSON line with kind=workspace and the
new control path. Use that workspace channel for subsequent scene/resource/Play/
export operations. Closing the editor removes the owned home control directory.
A response-write failure after effects remains terminal and reports that creation
may have committed; subsequent calls cannot hide that error or replay actions.
The created project remains available for reopening. This is local automation,
not an authenticated remote protocol.

Headless project-action tests cover template creation, source preservation and
validated project loading. CPU mailbox cases cover copied selection/error strings,
cancellation, one-shot consumption and worker completion after caller retirement.
Validate the actual home/workspace transition through its agent interface; computer
use supplements form navigation and native folder-picker interaction. Local
validation is arm64 macOS; Windows/Linux remain unvalidated.

Historical manual Release-app verification opened the system
picker through the Browse button, cancelled, selected a creation parent, created a
project and selected that existing folder through the picker to enter its workspace.
The Create page was visually inspected for aligned columns. Parent-picker
invocation used the shared command; project-picker invocation used a GUI click.
