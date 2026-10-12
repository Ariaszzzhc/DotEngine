# ESM runtime

`Dot.Script.Runtime` owns one QuickJS-NG 0.16.2 realm/context and a FileSystem
copy. Vendor types stay in the implementation unit. The library is a private
pinned vcpkg dependency; its libc/OS modules are not linked into the engine.
The API follows the [official embedding API](https://quickjs-ng.github.io/quickjs/developer-guide/intro/).
The [native TypeScript compiler](#native-typescript-compilation) produces ESM for
this layer. The [registered host bridge and core scene SDK](#registered-host-services)
are implemented together with persistent instances and the entity lifecycle service.
Source hot reload is implemented through the service below; remaining subsystem
APIs are still to be implemented.

Create a Runtime with a project FileSystem, load a canonical UTF-8 .js/.mjs entry,
and call an exported function through its scoped ModuleId. Entry paths are root
relative, have no empty/dot/parent components, drive prefixes, backslashes, control
characters, or trailing dot/space components. Paths are limited to 4096 bytes.
Imports must use ./ or ../ with an explicit .js/.mjs extension; normalized paths
must stay inside the root. Filesystem canonical checks also reject symlink escape.
Apart from the explicit registered `dot:host` module, bare packages, OS modules,
import attributes, extension guessing and bytecode loading are unsupported.
Source is bounded UTF-8 without raw NUL; escaped NUL is
valid string data but forbidden in import specifiers.

A small source patch in the versioned overlay port rejects embedded NUL in both
static and dynamic specifiers before conversion to a C-string alias can load a
different file. The manifest override pins the upstream version and local port
revision; project-wide vcpkg-configuration.json selects the overlay even when a
caller does not use CMake presets. Source/hash/license handling remains in vcpkg.
See [vcpkg configuration](https://learn.microsoft.com/en-us/vcpkg/reference/vcpkg-configuration-json)
for overlay resolution.

Modules cache per realm, including imports loaded later as entries. Repeated loads
reuse handles/state and same-path file edits do not reload automatically. Cycles,
relative parent imports, live bindings, dynamic imports, and top-level await are
supported. Loader/entry initialization failures fault the realm because resolution
can leave partial module state; a new realm is required for recovery. Stats retain
known compiled-definition/entry records after faults, rather than claiming that
all unresolved native module definitions remain alive.

`call` takes a JSON array with at most 64 arguments and returns an owned optional
JSON string. Argument views are copied into bounded terminated storage before the
SDK reads them. Empty/raw-NUL/bad UTF-8 inputs, invalid names, foreign/stale handles,
non-function exports, oversized JSON, and non-array/too-many arguments fail.
Numbers have normal JavaScript double precision; transport 64-bit entity IDs as
canonical decimal strings. Top-level undefined returns nullopt, while null returns
"null". Other results use JSON serialization/toJSON and enumerable string fields;
undefined/function/symbol/BigInt/nonfinite values, nested promises, enumerable
symbol keys, and cycles fail instead of being silently dropped/substituted.
Nonenumerable fields and prototypes are outside JSON data semantics.

Returned promises must settle using VM jobs. The host marks observed promises
handled, drains bounded jobs, and distinguishes explicit rejection from a promise
with no completing jobs. Unhandled rejections are owned/tracked until handled;
pending promises, infinite job chains, and tracker overflow fault the realm.
The same job counter covers execution and serialization within an operation.
No implicit timers, fetch, require, process, or std/os bindings are installed;
Atomics.wait is disabled. Ordinary clean exceptions are recoverable and include
module/export context and bounded diagnostics/stack when available; earlier script
mutations are not rolled back. Diagnostic getters remain under the execution budget.

Default limits are 16 MiB live VM allocator payload, 128 KiB JS stack, 1 MiB per
source and JSON buffer, 256 modules, 1024 jobs, and 100 ms cooperative execution per
operation. Configurable ranges are: memory 1 byte..1 GiB, stack 64..256 KiB,
source/JSON 1 byte..16 MiB, modules 1..1024, jobs 1..65536, and time 1..1000 ms.
Tiny memory budgets can fail runtime/context initialization and release partial
owners. Runtime payload allocation counters include arenas/backing allocations;
allocator headers, host containers/source copies, and OS internals are excluded.
The custom allocator bounds payload and checks calloc/realloc arithmetic; the
SDK's own quota also applies. Known SDK out-of-memory errors are classified as
faults even when quota rejection occurs before the custom allocator callback.
Caught builtin preflight exceptions can remain ordinary script control flow.
Stack overflow reports an exception; timeout/observed VM allocation/job/loader
failures fault the runtime. This is a cooperative VM bound, not a hostile-code
process sandbox or a filesystem/compilation/destruction real-time guarantee.

All instance operations require the creating thread and reject reentry. Moves
preserve runtime/handle identity; quiescent destruction may happen elsewhere.
Owned JS values, rejection reasons/promises, and namespaces release before the
context/runtime. SDK callbacks contain C++ exceptions rather than unwinding over
the C ABI. Faulted statistics remain queryable; calls/loads require a new realm.
`backendMemory` reports process-wide live allocator payload for lifetime checks.

Run `DotRuntime --script-test <existing-directory>` for an actual file-backed ESM
graph, async JSON calls, shared state, recoverable exceptions, execution interruption,
and complete allocator cleanup. Unit tests also cover malformed inputs, import
paths/attributes/NUL, strict results, memory/job/stack limits, diagnostic getters,
failed initialization, cycles, cache identity, thread/move/foreign destruction,
and lifetime. Only macOS Debug/Release are validated; no sanitizers are used.

# Native TypeScript compilation

`Dot.Script.Compiler` uses the official standalone Go compiler from the pinned
TypeScript 7.0.2 native packages. The vcpkg host dependency selects darwin-arm64,
win32-x64 or linux-x64, verifies SHA512 and installs the executable and standard
declarations together in `tools/typescript-native`. No Node/npm runtime is used.
Vendor executable permissions and signatures are preserved; automatic rpath
rewriting is disabled for this standalone tool. See the
[official TypeScript package](https://www.npmjs.com/package/typescript) and
[compiler options](https://www.typescriptlang.org/tsconfig/).

Create a `TypeScriptCompiler` with an absolute executable path. Initialization
runs a bounded version probe and requires 7.0.2; missing/incompatible binaries or
missing adjacent declarations produce contextual errors. CMake locates only the
manifest-installed tool (explicit host triplet when supplied, otherwise the native
target triplet). Application callers pass its location rather than embedding a
machine-specific toolchain path into the library.

`compileProject(project, "scripts")` captures a directory tree into a fresh owned
temporary generation. Canonical relative UTF-8 paths share the runtime's portable
character/component rules. Symlinks, special files and directory depth above 32
are rejected. Lowercase `.ts`/`.mts` sources and `.d.ts`/`.d.mts` declarations are
copied; `.tsx`/`.cts`/`.js`/`.mjs`/`.jsx`/`.cjs` files fail explicitly, other regular
files are ignored. Source files must be UTF-8 without NUL. No source edits may race
capture; these checks do not isolate hostile concurrent filesystem mutation.

The generated configuration uses strict ES2022, ESNext modules, Bundler module
resolution, forced module detection, isolated modules, verbatim module syntax,
noEmitOnError, noUncheckedSideEffectImports, consistent casing, noResolve, no ambient
package types and the ES2022 standard library. An explicit file list supplies all
snapshot sources, including referenced relative modules. Project tsconfig files
are ignored. Imports name the emitted `.js`/`.mjs` files; noResolve prevents the
compiler from implicitly adding project-external sources. ESM paths/imports must
also satisfy the Runtime loader contract when activated. Projects copy the
versioned `assets/scripts/dot` SDK into their source tree for the implemented
services. Source maps contain the original TypeScript text and relative paths
into the retained snapshot.

Successful compilation checks actual regular output files against every expected
module and source map, with UTF-8 and aggregate byte checks. Zero exit status with
no/missing/unexpected output is an error. Nonzero exits return the actual status
and captured diagnostics; process startup/I/O/timeout/output-limit failures retain
their context. Every failure discards the candidate. A new successful generation
does not overwrite previous generations or authoring files.

`CompiledProgram` is a copyable shared RAII owner. Query its output FileSystem,
sorted module names, owned diagnostics and source/output statistics. Keep a copy
alive while any Runtime accesses this output root, including lazy dynamic imports;
FileSystem itself does not own files. Do not edit generated files. Last-owner
destruction removes the full generation directory best effort. Moves leave the
source query-invalid; quiescent destruction may run on another thread. Independent
compiler calls may run concurrently. `compilerGenerations()` counts process-wide
live generation owners for quiescent lifetime checks.

Default limits are 1024 source files, 8192 enumerated source entries, 1 MiB per
source, 16 MiB total source, 32 MiB actual output, 1 MiB combined diagnostics, and
10 seconds per tool invocation. Configurable ceilings are 1024 files, 65536 entries,
16 MiB per source, 64 MiB total source/output/diagnostics, and 10 minutes; each limit
must be positive. Capture and output inspection are bounded, and the Process layer
owns termination/reaping. These are not OS memory/disk quotas or hard real-time
guarantees: output size is checked after the compiler finishes, and generation
destruction cannot report filesystem deletion failures.

Run `DotRuntime --typescript-test <absolute-compiler> <existing-directory>` for real
type checking, ESM emission/execution, source maps, deliberate type diagnostics,
continued old-runtime execution and complete generation/process/VM cleanup.
Focused tests include actual Unicode/mts/declaration compilation, source maps,
lazy imports after authoring removal, configuration isolation, missing imports,
exact resource boundaries, failed candidates, copy/move/lifetime/concurrency and
test-only tool fixtures for zero/partial/unexpected output, timeout and oversized
diagnostics. These fixtures never substitute for the real compiler acceptance.

# Registered host services

`Dot.Script.Host` defines an owned `HostBindings` list supplied as the third
argument to `Runtime::create`. The runtime copies at most 256 unique nonempty
callbacks and never changes them. Names are 1..128 ASCII bytes with dot-separated
identifier segments: each starts with a letter/underscore and continues with
letters/digits/underscores. The creating thread invokes callbacks synchronously.
Use owned captures, or keep borrowed dependencies alive; views of arguments last
only through the callback. Moving the Runtime preserves its private context owner.
Do not move/destroy a Runtime from inside its active callback. Reentrant public
loads/calls/statistics are rejected.

An empty registry exposes no native module. Registered realms permit only the
reserved `import {invoke} from 'dot:host'`; the function is not installed globally.
Static/dynamic imports and top-level await share the same native module, which
counts toward `maxModules`. Other bare names, attributes and NUL aliases still
fail. `invoke(method, arguments)` looks up exactly the registered method and
validates a finite JSON argument array of at most 64 values, under `jsonBytes`.
Serialization follows the strict runtime replacer, including `toJSON`/getter
semantics, then requires a scalar-Unicode JSON array. Missing/invalid methods or
arguments fail before the selected native callback runs. Argument getters can
recursively invoke services; shared limits are checked again before the outer
callback to prevent side effects after a caught budget failure.

Callbacks return owned finite scalar-Unicode JSON or nullopt for JavaScript
undefined; JSON null is distinct. Invalid/oversized/empty/NUL/bad-UTF-8/overflowing
results fail explicitly. VM parsing failures retain native method context, including
valid JSON that exceeds the VM stack. Expected Core failures and contained C++
exceptions become catchable Error objects named `DotHostError`, with `code` equal
to `InvalidArgument`, `NotFound`, `Io`, `Unavailable`, `InvalidState` or `External`.
Messages include the host method and a bounded UTF-8 diagnostic. Own properties
are defined without invoking script-controlled Error prototype setters. Ordinary
clean failures may be caught or returned as contextual C++ call errors; earlier
script/native side effects are not rolled back. This bridge is not a transaction
manager or a hostile-native-code sandbox.

`maxHostCalls` defaults to 256, configurable to 1..65536. The same counter covers
initializers, calls, pending jobs and result/argument serialization within one
public operation. Unknown/invalid attempts consume the budget too. Catching an
over-budget exception cannot make the operation successful; the realm faults.
Native work cannot be preempted. Time is checked around it and after the operation;
observed excess faults the realm even if script catches its exception. C++ callback
allocations/JSON transport containers are outside the VM allocator quota; argument
and returned byte bounds do not limit how much a callback itself allocates.

# Core scene SDK

`Dot.Script.SceneHost::sceneBindings(sharedScene)` produces eleven methods and
retains shared ownership of the original scene identity. Creation and calls require
the native scene's creating thread. Moving/replacing that Scene invalidates all old
bindings explicitly. Destroying native entities invalidates wrappers. Entity IDs
are `{scene: string, value: string}` canonical positive decimal uint64 strings
(excluding the reserved maximum), preserving values above JavaScript's safe integer
range. Numeric IDs, leading zeros, signs, unknown fields and foreign/stale handles
fail instead of aliasing another entity.

| Native method | Arguments | Result |
| --- | --- | --- |
| `scene.info` | `[]` | `{id, name, entities}` |
| `scene.rename` | `[name]` | undefined |
| `entity.list` | `[]` | IDs in creation order |
| `entity.create` | `[name]` | ID |
| `entity.get` | `[id]` | owned `{id, name, parent, transform}` snapshot |
| `entity.destroy` | `[id]` | undefined; deletes the entire subtree |
| `entity.rename` | `[id, name]` | undefined |
| `transform.get` | `[id]` | owned local TRS snapshot |
| `transform.set` | `[id, transform]` | undefined |
| `transform.world` | `[id]` | sixteen column-major float values |
| `hierarchy.setParent` | `[id, parentId or null]` | undefined |

The complete transform shape is `{position: {x,y,z}, rotation: {x,y,z,w}, scale:
{x,y,z}}`, with finite float-range numbers and no extra/missing fields. The native
scene normalizes quaternions and validates nonzero scales, hierarchy/world overflow
and cycles before each mutation. Name, thread, scope and subtree rules are the same
as direct native calls. Getters return owned snapshots; editing one has no native
effect until assignment. No mesh/camera/audio/physics component fields are included
in this core snapshot yet; these remain separate API work.

Copy `assets/scripts/dot/engine.ts` and `host.d.ts` into a project's `scripts/dot`
directory, then compile that source tree with TypeScriptCompiler. The declaration
resolves the reserved module; the TypeScript implementation exposes `scene` and an
`Entity` view, typed IDs/vectors/quaternions/transforms/snapshots and HostError codes.
These wrappers own copied identity/snapshots and do not own native entities.

```ts
import {Entity} from './dot/engine.js';
import type {EntityId} from './dot/engine.js';

let actor: Entity;

export function init(id: EntityId): void {
  actor = new Entity(id);
}

export function tick(seconds: number): void {
  const transform = actor.transform;
  transform.position.x += seconds;
  actor.transform = transform;
}
```

This example uses exported functions as a runnable host/SDK acceptance layer;
the entity-attached lifecycle service below provides scheduled instances. Candidate
load/initializer failures through this direct bridge can have native side effects,
so it must not be mistaken for transactional hot reload.

Run `DotRuntime --script-host-test <absolute-compiler> <sdk-root> <directory>` for
the actual TypeScript SDK, native entity creation/deletion and parenting, renaming,
recoverable transform rejection, sixty typed transform ticks, and compiler/process/
VM cleanup. `<sdk-root>` contains `dot/engine.ts` and `dot/host.d.ts`.
`--script-host-render-test` takes the same arguments and also checks actual GPU
pixels before/after script-driven movement of a native cube. macOS uses Metal;
Windows/Linux implementations are not validated locally. No sanitizers are used.

# Persistent script objects

`Runtime::createInstance(module, constructor, arguments)` invokes an exported
constructable class/function with the same bounded JSON array transport as calls.
It retains the actual object, including its prototype, private fields and closure
state, and returns an InstanceId scoped to that realm. Default capacity is 1024
live instances, configurable to 1..10000. Capacity/identity checks precede
constructor side effects. IDs are monotonic and never reused after release.

Constructors are synchronous. Returned promises are marked handled and bounded
jobs are drained before rejecting the result; an async/arrow/nonconstructable
export fails. Ordinary constructor exceptions release partial host ownership and
preserve a clean realm, with module/class context. Earlier script/native mutations
are not rolled back. JavaScript constructor semantics still apply: a function may
return another object shared with other instances instead of its newly created
receiver. Normal class construction produces separate state per invocation.

`callMethod(instance, method, arguments)` supplies the owned object as `this` and
supports inherited methods, private state and bounded async results. Argument
validation precedes property lookup, so malformed requests cannot trigger method
getters. Missing/noncallable methods are errors. Getter evaluation and call/jobs/
serialization share the operation's resource budgets. Noncallable getter results
still drain their pending jobs before failure. Errors include class/instance/method
context, while clean exceptions retain earlier instance/native mutations.

`hasMethod` supports optional hook discovery: undefined means absent, while present
nonfunctions (including null) fail. Getters may run and enqueue bounded jobs; this
is not pure reflection and a later call performs a fresh lookup. `releaseInstance`
removes the runtime's strong reference without invoking a lifecycle callback. Other
script references/cycles may keep the object alive; their VM storage is reclaimed
by normal collection or realm destruction. Released/foreign handles fail. Release
is allowed on a faulted realm but still requires its owner thread without reentry.
Moves preserve instances; quiescent realm destruction may occur on another thread.
`RuntimeStats.instances` reports live host-held handles, not every object in JS.

Run `DotRuntime --script-instance-test <absolute-compiler> <sdk-root> <directory>`
for actual TypeScript classes with private state and inherited hooks, two native
entities driven independently through sixty async updates, contextual failure
recovery, explicit stop calls, stale-handle rejection and complete compiler/process/
VM cleanup. This acceptance caller invokes hooks explicitly; the entity lifecycle
service below provides attached descriptions, persistence and scheduling. Hot
reload remains to be implemented.

# Entity-attached lifecycle service

`Dot.Script.Source::ComponentProperties` and `World::Scene::setScript` attach a
single owned authoring description to an entity. Scene snapshots/save/load retain
its source path, class export, canonical property JSON and enabled flag. They
initialize no realm or compiler. Paths are relative to the TypeScriptCompiler
source directory: actors/player.ts maps to actors/player.js in the compiled
output, and .mts maps to .mjs. Property objects are bounded to 64 KiB/depth 32;
source paths/identifiers/finite Unicode/duplicate fields are validated before an
edit. `SceneFiles` persists this as an optional version-1 script object.

`SceneScripts::create(sharedScene, compiledProgram, config)` owns a generation
copy and an independent Runtime, and retains the original shared scene identity.
The caller may release its generation/scene copies. Operations require the
creating thread without reentry; moving the service preserves its private owner.
Moving/replacing the scene invalidates operations. Quiescent destruction may run
elsewhere and releases all storage without executing script; use stop explicitly
for application cleanup callbacks.

`sync()` captures authoring in creation-ID order and prepares every changed
candidate before stopping/replacing existing objects. Unchanged descriptions keep
instance state. Disabled descriptions load no module/create no instance; enabling
or changing a description creates a fresh object. Source-file changes within a
compiled generation are not reload: module definitions still cache per realm.
Preparation loads the emitted module, constructs the exported class with one
owned context argument, and discovers optional onStart/onUpdate/onFixedUpdate/
onStop methods. Noncallable fields are failures. The constructor context is
`{entity: {scene,value}, properties: {...}}`; the source SDK provides typed
ScriptContext and ScriptComponent with an Entity view and optional no-action hooks.
Top-level properties are frozen snapshots; nested values are owned data.

During module initialization, construction and hook discovery, native scene reads
are allowed and scene mutations fail with contextual InvalidState errors. Constructors
should initialize script state and place native edits in lifecycle hooks. Ordinary
candidate preparation failure keeps existing objects; dispatch reports the failure
until authoring is repaired. Realm-global JS mutations are not rolled back. Loader/
resource faults release all instance handles and require a new service. This is
preparation of component edits, not transactional source hot reload.

`start()` synchronizes and starts pending objects once. `update(seconds)` and
`fixedUpdate(seconds)` also synchronize/start first, then dispatch their respective
hooks. All pending starts finish in creation order before frame callbacks; both
seconds values must be finite in [0,1]. Hooks return void/Promise<void>, with the
Runtime's bounded jobs and strict results; other return values fail explicitly.
Side effects are visible to later callbacks. Owners deleted during callbacks are
skipped/pruned, including self-deletion during startup/update. Removing/disabling/
replacing a started component invokes onStop once before releasing its handle.
Stop side effects may delete prepared owners; such candidates are released and
reported rather than activated with stale IDs. Lost owners can make onStop fail.

A successful Result<ScriptReport> means the dispatch itself completed. Check
failures and realmFaulted to establish script success: each failure carries the
entity, phase and actual Core error. Ordinary callback failures attempt one stop,
release that object and mark its description failed while other objects continue.
Unchanged failed descriptions do not repeatedly rerun; change the description or
create a fresh service to retry. Realm faults stop further dispatch and release
all handles. Native/global script side effects are not rolled back. Unexpected
native exceptions fault the service; stop/statistics remain available for cleanup.

`stop()` is terminal/idempotent, attempts stops on started objects and releases
all handles even when callbacks fail. Destruction never substitutes callbacks.
Stats report descriptions, pending/running/disabled/failed counts, stopped/faulted
state and actual Runtime stats. Default capacity is 512 descriptions, configurable
to 1..5000, including disabled records. Runtime instance capacity must be at least
twice that configured limit for staging at full capacity (default Runtime 1024,
maximum 10000). Invalid combinations/capacity excess fail before lifecycle changes.
VM/source/job/time/JSON limits remain the Runtime's; host containers are outside its
allocator quota. Failed preparation may leave retained module-global JS state;
stopping releases host objects but does not reset module globals.

```ts
import {ScriptComponent} from './dot/engine.js';

export class Player extends ScriptComponent<{speed: number}> {
  onUpdate(seconds: number): void {
    const transform = this.self.transform;
    transform.position.x += this.properties.speed * seconds;
    this.self.transform = transform;
  }
}
```

Run `--script-components-test <compiler> <sdk-root> <directory>` to save/load an
actual authored scene, compile the source SDK/classes, automatically dispatch
start/sixty async updates/fixed updates/stop on two independent native entities,
and verify original handles are foreign after load plus full compiler/process/VM
cleanup. `--script-components-render-test` also validates real GPU pixels before/
after lifecycle-driven native movement. Only macOS Debug/Release are validated.
The source hot-reload service below extends this lifecycle layer. Remaining
subsystem APIs, editor/project/export and the example game remain future work.

# Source hot reload

`SceneScripts::reload(newProgram)` prepares a wholly independent realm/generation
and all current component candidates before calling old state hooks. Constructor,
initializer or candidate migration failure discards the candidate and retains the
old service/native scene. Existing generations remain immutable; source edits are
compiled into new owned output directories.

Compatible running components match by entity ID, source path and class export.
Optional onSaveState() on the old instance must return finite scalar-Unicode JSON;
the candidate must then provide onLoadState(state), returning void/Promise<void>.
Private fields can be explicitly exported/restored through those hooks. Aggregate
serialized state defaults to 8 MiB, configurable to 1 byte..64 MiB, in addition to
Runtime per-result/per-argument limits and codec depth 64. Undefined exports,
missing loaders, nonvoid imports, malformed/deep Unicode JSON and excess bytes
fail explicitly. Pending/disabled/failed instances and changed types do not migrate.

Native scene writes are denied throughout preparation, export and import. Save
hooks must be observational: their own arbitrary JS mutations cannot be rolled
back, and a save-hook VM resource failure can fault the old realm. Candidate
initialization is completed before any old save hooks run. A faulted old realm
can be replaced without exporting state. Native side effects are unavailable
while preparing; source compiler/candidate failures leave the old generation
available for subsequent frames.

Successful switching preserves running status and native scene state for compatible
objects: old onStop/new onStart are skipped to avoid tearing down/recreating
native game state. New/pending descriptions start through the normal scheduler.
Removed, disabled or changed-type old components stop after commit. ReloadReport
contains their explicit cleanup report; inspect retired.failures/realmFaulted as
well as migration counts. Cleanup side effects may remove candidate owners, which
are released/reported instead of retained as stale bindings. Cleanup failures do
not silently roll back an already committed generation. Unexpected native cleanup
exceptions after commit fault the new service and identify that commit in the error.
The whole private owner is swapped, preserving each object guard's Runtime address;
old realm/program storage releases after migration/cleanup scopes end.

```ts
export class Player extends ScriptComponent {
  #ticks = 0;

  onSaveState(): {ticks: number} {
    return {ticks: this.#ticks};
  }

  onLoadState(state: {ticks: number}): void {
    this.#ticks = state.ticks;
  }
}
```

`SourceSnapshot` is an immutable shared capture of canonical source paths and actual
UTF-8 bytes. captureProject applies the existing file/entry/depth/byte boundaries;
compileSnapshot rechecks source count/bytes and emits those exact captured bytes
without rereading authoring. equals uses exact path/content comparison, avoiding
hash collisions and timestamp-only recompilation. Moved-from snapshots are invalid;
no open authoring files are retained. Captures use the same source filtering and
configuration isolation as normal TypeScript compilation.

`Dot.Script.LiveScripts` wraps a project/compiler and SceneScripts for automatic
content polling. Default directory is scripts and interval is 100 ms, configurable
to 0..60000 ms. update/fixedUpdate poll before normal lifecycle dispatch, retaining
old code when capture/compilation/candidate preparation fails. Invalid deltas fail
before polling. Failed source contents are remembered, so unchanged failures are
not repeatedly compiled. poll(true) bypasses time/content checks and retries;
source changes naturally retry. Capture failure diagnostics clear when identical
valid sources are restored, without recompiling unchanged code. Status explicitly
reports checks/changes/commit/error/reload cleanup; stats count actual compiler
attempts and committed swaps. Operations require the original owner thread/scene,
without reentry. stop is terminal; destruction releases storage without script.

Polling/capture/compilation are synchronous and not a hard frame-time guarantee;
compiler invocations retain Process time/output bounds. Captured text and native
transport containers are outside the VM allocator quota. This version uses
portable content capture rather than platform notification APIs.

Run `--script-reload-test <compiler> <sdk-root> <directory>` for actual source edits,
type-error retention while old code continues, new code compiled into another
realm, private state migration without duplicate startup/cleanup, changed native
movement and compiler/process/VM teardown. `--script-reload-render-test` also checks
actual GPU pixels before/after the reloaded behavior. macOS Debug/Release are
validated; Windows/Linux use common C++ implementations but remain unverified.
