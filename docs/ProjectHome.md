# Project home

Starting DotEditor with no arguments opens the native project home. It opens an
existing project folder or creates a validated project from Camera and cube,
Prism Relay or the component/SDK laboratory. All three creation choices publish
one new folder under an existing parent, using the embedded engine modules and packaged compiler.
Existing directories/files/symlinks are preserved. Select a project to enter the
normal editor workspace; the home GUI/video resources release before that window
opens. An open/create failure stays visible in the home for correction.

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

Inspect returns the three templates, running state and selected root; close ends
the selection loop. Request IDs use 1..64 ASCII letters/digits/_/- and may not be
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

Native acceptance renders the home, creates/selects via the shared mailbox, reads
its response, releases the GUI, opens/renders the chosen workspace and runs Play/
Stop. Other cases cover source preservation/engine module ownership, invalid inputs/compiler
failures/no overwrite, all three template choices, thread/move/lifetime checks,
response failure after committed creation and real CLI arguments/budgets. Local
validation is arm64 macOS; Windows/Linux remain unvalidated. No manual mouse/
desktop acceptance is claimed.
