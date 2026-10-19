# ESM runtime

`Dot.Script.Runtime` owns one QuickJS-NG 0.16.2 realm/context and a FileSystem
copy. Vendor types stay in the implementation unit. The library is pinned as a
private source dependency under `vendor/quickjs-ng`; its libc/OS modules are not linked into the engine.
The API follows the [official embedding API](https://quickjs-ng.github.io/quickjs/developer-guide/intro/).
The [native TypeScript compiler](#native-typescript-compilation) produces ESM for
this layer. The [registered host bridge and core scene SDK](#registered-host-services)
are implemented together with persistent instances and the entity lifecycle service.
Source hot reload and native scene/component/input/physics/audio APIs are implemented
through the services below. See [example workflows](Acceptance.md) for SDK evidence.

Create a Runtime with a project FileSystem, load a canonical UTF-8 .js/.mjs entry,
and call an exported function through its scoped ModuleId. Entry paths are root
relative, have no empty/dot/parent components, drive prefixes, backslashes, control
characters, or trailing dot/space components. Paths are limited to 4096 bytes.
Project imports use extensionless ./ or ../ names and resolve to exactly one
regular compiled .js/.mjs file within the root. Missing files, ambiguous outputs,
explicit extensions, case aliases and symlink components fail. Directory/index
and package guessing are unsupported. Engine modules use exact registered names
such as `dot:engine` and the native `dot:host`; unknown `dot:` names never fall back
to project files. Bare packages, OS modules, import attributes and bytecode loading
are unsupported. Entry paths remain compiled filenames, separate from imports.

```ts
import {Entity, input, ScriptComponent, type Transform} from 'dot:engine';
import {settings} from './config/settings';
```

The engine compiles its sole SDK implementation at build time and embeds its ESM
and declarations in the static library. Projects, templates and published game
module trees contain only their own sources/modules. `Dot.Script.Modules` owns the
registry shared by compiler and runtime; future engine modules use this registry.
Source is bounded UTF-8 without raw NUL; escaped NUL is
valid string data but forbidden in import specifiers.

A small project patch applied to the fixed QuickJS-NG subtree rejects embedded
NUL in both static and dynamic specifiers before conversion to a C-string alias
can load a different file. The upstream revision and patch are recorded under
`vendor/` and `cmake/patches/`; CMake links the local `qjs` target.

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
source and JSON buffer, 256 project modules, 1024 jobs, and 100 ms cooperative execution per
operation. Configurable ranges are: memory 1 byte..1 GiB, stack 64..256 KiB,
source/JSON 1 byte..16 MiB, modules 1..1024, jobs 1..65536, and time 1..1000 ms.
The fixed engine registry is separate from the project-module quota; statistics
include loaded engine modules and their VM allocations still count toward memory.
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

# Native TypeScript compilation

`Dot.Script.Compiler` uses the official standalone Go compiler from the pinned
TypeScript 7.0.2 native packages. FetchContent selects darwin-arm64, win32-x64 or
linux-x64 for the build host, verifies SHA-512 and exposes the executable and
standard declarations from one archive. No Node/npm runtime is used. Release
executable permissions and signatures are preserved; the standalone tool is not
rewritten for rpath. See the
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
are ignored. Relative imports omit extensions; engine imports use `dot:` names.
Engine-owned ambient declarations are injected outside the project source tree
and source statistics. noResolve prevents implicitly adding external sources.
For `.mts`, private declaration aliases direct extensionless type resolution to
its actual exports while retaining `.mjs` output. Source maps contain the original
TypeScript text and relative paths into the retained snapshot.

After emission, the shared runtime loader parses and links the entire static graph
without executing initializers, constructors or host services. Unknown engine names,
explicit project extensions, missing/ambiguous imports and invalid exports discard
the candidate before Check/Play publication. Dynamic imports use the same resolver
when executed. Graph validation uses a private 256 MiB VM, 16 MiB per source, 1024
project modules and one second of cooperative time per graph; filesystem waits are
not an OS sandbox or hard wall-clock limit. These VM allocations release on both
success and failure.

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

Every realm provides the reserved `import {invoke} from 'dot:host'`; the function
is not installed globally. Static/dynamic imports and top-level await share the
same native module, separate from the project-module quota. An empty callback
registry allows importing engine types/constants but calling an unregistered
service raises a contextual `DotHostError`. Other bare names, attributes and NUL
aliases still fail. `invoke(method, arguments)` looks up exactly the registered method and
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

`Dot.Script.SceneHost::sceneBindings(sharedScene)` produces native scene methods and
retains shared ownership of the original scene identity. Creation and calls require
the native scene's creating thread. Moving/replacing that Scene invalidates all old
bindings explicitly. Destroying native entities invalidates wrappers. Entity IDs
are `{scene: string, value: string}` canonical positive decimal uint64 strings
(excluding the reserved maximum), preserving values above JavaScript's safe integer
range. Numeric IDs, leading zeros, signs, unknown fields and foreign/stale handles
fail instead of aliasing another entity.

`scene.findEntities(name)` performs an exact UTF-8 name scan in native storage and
creates wrappers only for matches. It returns every duplicate in deterministic
creation-ID order, so callers requiring a unique role must check the count. Missing
names return an empty array; empty, NUL-containing, invalid UTF-8 or over-256-byte
names fail. It is read-only, including during script preparation. This avoids
spending one host call per unrelated entity on `.entities().find(e => e.name === name)`;
the shared per-invocation host-call/time/memory limits are unchanged.

`Entity.children` reads only native hierarchy/identity storage and returns fresh
wrappers for direct children in creation-ID order. A live leaf returns an empty
array; missing/foreign/wrong-thread parents fail. It is read-only during preparation.
Array membership is owned, while wrappers retain their usual live, scene-scoped
identity: reparenting/deletion can make retained members no longer children or no
longer live. Mutating the returned array never changes native hierarchy. Prism
characters bind their own single model child instead of a global unique model name,
so whole-subtree copies retain independent remapped rigs. The strict host method is
`entity.children` with exactly one canonical scoped entity ID.

| Native method | Arguments | Result |
| --- | --- | --- |
| `scene.info` | `[]` | `{id, name, entities}` |
| `scene.rename` | `[name]` | undefined |
| `entity.list` | `[]` | IDs in creation order |
| `entity.findNamed` | `[name]` | all exact name matches in creation order; missing returns `[]` |
| `entity.create` | `[name]` | ID |
| `entity.get` | `[id]` | owned identity, name, parent, transform and component snapshots |
| `entity.destroy` | `[id]` | undefined; deletes the entire subtree |
| `entity.rename` | `[id, name]` | undefined |
| `transform.get` | `[id]` | owned local TRS snapshot |
| `transform.set` | `[id, transform]` | undefined |
| `transform.world` | `[id]` | sixteen column-major float values |
| `hierarchy.setParent` | `[id, parentId or null]` | undefined |
| `entity.exists` | `[id]` | boolean; foreign/invalid IDs still fail |
| `component.get` | `[id, name]` | owned description or null |
| `component.set` | `[id, name, description or null]` | undefined |
| `scene.primaryCamera` | `[]` | primary camera ID or null |
| `scene.primaryListener` | `[]` | primary audio listener ID or null |

The complete transform shape is `{position: {x,y,z}, rotation: {x,y,z,w}, scale:
{x,y,z}}`, with finite float-range numbers and no extra/missing fields. The native
scene normalizes quaternions and validates nonzero scales, hierarchy/world overflow
and cycles before each mutation. Name, thread, scope and subtree rules are the same
as direct native calls. Getters return owned snapshots; editing one has no native
effect until assignment. The snapshot includes nullable `camera`, `mesh`,
`material`, `rigidBody`, `character`, `audioSource`, `audioListener`, and `script` descriptions. Those exact
names are accepted by component.get/set. Null removal is idempotent for a live
entity; a destroyed owner remains an explicit error. A missing primary component
is null. `Entity.exists` is false for missing IDs in this scene, while foreign,
invalid and moved-scene handles remain errors.

Import the embedded `dot:engine` module directly. TypeScriptCompiler supplies its
matching declarations; no SDK installation/copy step is needed. It exposes `scene`
and an `Entity` view, typed IDs/vectors/quaternions/transforms/snapshots and HostError
codes. These wrappers own copied identity/snapshots and do not own native entities.

Component fields use the native authoring contracts, with object vectors `{x,y,z}`
and colors `{x,y,z,w}`. Complete assignments require every documented field and
reject unknown fields, invalid enum strings, wrong scalar types, float overflow and native
range violations before mutation. Source paths and primary uniqueness use native
validation. Script properties are a bounded JSON object, not an encoded JSON
string. Direct callback input rejects duplicate fields and nesting beyond 64;
script properties retain their separate depth-32/64-KiB limits. Authoring reads no
assets and initializes no physics/audio/script backend; file availability and
backend-specific constraints are checked by the consuming services.

`components` creates fresh descriptions matching native defaults. Camera, mesh,
and rigidBody accept shallow `Partial` overrides that replace complete fields;
these are convenience constructors, and assignment still validates all fields.
Material bindings require a portable `.dotmaterial` asset path; the consuming
renderer loads the resource. Audio sources require an asset path, and script descriptions
require a source path. Default nested objects are independent; caller-supplied override objects
remain ordinary caller-owned values until an assignment copies them.

```ts
import {scene, components} from 'dot:engine';

const actor = scene.create('Actor');
actor.mesh = components.mesh({tint: {x: 0, y: 1, z: 0, w: 1}});
actor.material = components.material('assets/surface.dotmaterial');
actor.rigidBody = components.rigidBody({motion: 'kinematic', shape: 'box'});
actor.audioSource = components.audioSource('audio/theme.wav');
actor.script = components.script('actors/mover.ts', 'Mover');

const source = actor.audioSource!;
source.voice.looping = true;
actor.audioSource = source;
actor.rigidBody = null;
```

Constructors and state migration may query these descriptions/primary
entities/existence, while component assignment is denied during preparation and
save/load just like earlier scene writes. Lifecycle callbacks may edit authoring;
self-disable takes effect when the next lifecycle dispatch synchronizes the scene.

```ts
import {Entity} from 'dot:engine';
import type {EntityId} from 'dot:engine';

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

This example calls exported functions through the native host/SDK bridge;
the entity-attached lifecycle service below provides scheduled instances. Candidate
load/initializer failures through this direct bridge can have native side effects,
so it must not be mistaken for transactional hot reload.

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
import {ScriptComponent} from 'dot:engine';

export class Player extends ScriptComponent<{speed: number}> {
  onUpdate(seconds: number): void {
    const transform = this.self.transform;
    transform.position.x += this.properties.speed * seconds;
    this.self.transform = transform;
  }
}
```

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
configuration isolation as normal TypeScript compilation. CompiledProgram.source()
retains that exact owned provenance for source-compiled generations. JS-only imports
from loadPublished reject the query; publishing still omits all TypeScript.

`Dot.Script.Compilation` provides CompilationJob for a single owned background
compileSnapshot call. ready/source/take require its creating thread; ready and take
never wait for compiler completion. take consumes one result. Stop is a quiescent
cancellation request; destruction joins and reaps the worker/process. The worker
has no active scene/VM/services. Hot reload and initial editor Play share this
implementation rather than creating separate background compilers.

`Dot.Script.LiveScripts` wraps a project/compiler and SceneScripts for automatic
content polling. Default directory is scripts and interval is 100 ms, configurable
to 0..60000 ms. The create convenience factory compiles synchronously; createPrepared
accepts an owned source-compiled generation only after exact current-source validation,
with runtime construction on the owner and no compiler invocation. Its initial
compileAttempts is zero; create includes its own initial attempt. Editor Play uses
the prepared factory after its separately counted startup job. Source-watching
scene transitions use current source rather than retaining the initial fixed-program
cache. Runtime preparation remains synchronous.
During update/fixedUpdate, the owner captures bounded source bytes and launches at
most one owned background compilation. The worker uses only that immutable snapshot
and a compiler descriptor, including an independent validation VM; it cannot access
the active scene, script VM or services. The current generation keeps dispatching
while compilation runs. Completion is checked without waiting for the compiler.

Before publishing a completed result, the owner captures current source again and
compares exact path/content bytes. Changed or unavailable source cancels and retires
the obsolete job; its result cannot migrate. The next job uses the latest valid
capture, rather than queuing every intermediate edit. Candidate runtime preparation,
state export/import, cleanup and publication still execute on the owner through the
ordinary transactional reload. Compilation/candidate errors retain old execution.
Capture errors take precedence while source is unavailable; restoring it clears the
capture error but retains any last compilation error until successful replacement.

Identical rejected source is remembered and is not automatically retried. poll(true)
bypasses time/content checks to schedule an unchanged retry; it returns compiling,
not synchronous completion. Repeated force during a current job coalesces into that
job. A force received while an obsolete job retires retains one retry for current
source, including a return to already active bytes; capture failure or explicit
cancellation clears that deferred request. ReloadStatus reports
checked/changed/compiling/discarded/committed, errors and
reload cleanup. LiveScriptsStats reports compilation attempts (including launch
failure), committed reloads, pending compilation and obsolete jobs retired while
running. Queries never collect/commit results. Completion is observed even before
the next ordinary source interval. Invalid frame deltas fail before polling/effects.

GameSession::pollScripts and Editor::PlaySession::pollScripts expose the same owner
operation without advancing gameplay timing/input or dispatching frame hooks. They
require a running source session with hot reload enabled. Compiled exports, unstarted,
closed, moved or wrong-thread sessions reject it. See the editor's shared
[retry/status operations](Editor.md#live-script-compilation).

Stop/destruction and game-session faults cancel outstanding work, join the owned
worker and reap the compiler's
process group/job. Core::ProcessConfig.stop prevents launch or returns an Unavailable
cancellation error while terminating/reaping owned processes, including descendants
retaining output pipes. TypeScriptCompiler::compileSnapshot accepts the same stop
token and discards private generation data; partial compiler output is never published.
The process timeout/output limits remain in force. Cancellation, filesystem cleanup
and OS teardown are not hard real-time guarantees. No worker is detached and no task
retains the scene after the controller retires.

源码捕获仍在所有者线程；运行中单个后台任务编译不可变快照，旧代继续运行。提交前重新捕获
核对字节，改变/失效的源码取消旧任务，不发布过期结果；只编译最新有效捕获，不排队每次
编辑。候选虚拟机准备、状态迁移及提交仍在所有者线程，失败保留旧执行。强制重试为异步，
当前同源任务合并请求；查询不收集/提交结果。停止/销毁取消、回收线程与编译进程及其后代，
不保留活动场景引用、不发布部分输出，不保证操作系统回收的硬实时延迟。

Source capture, initial preparation, migration and retired-generation filesystem
cleanup remain synchronous; this is not a hard frame-time guarantee or general
background filesystem service. Source edits must still be serialized against each
bounded capture. Captured text and native transport containers are outside the VM
allocator quota. Content capture remains portable, without platform watcher APIs.

# Input frames and native services

`Dot.Script.InputSource` owns an explicitly published native input snapshot. Create
it on the application's owner thread, copy its ten bindings into
`SceneScriptsConfig.services` (or `LiveScriptsConfig.scripts.services`), and publish
an initial frame before preparing classes that read input. Moves preserve existing
bindings; those bindings retain the last data after the publisher is destroyed.
A publisher creates no window or event pump, and all calls/publication require its
creating thread. Destruction after quiescence may occur elsewhere.

`LoopCallbacks.prepareFrame` runs once after event consumption, before any fixed
or frame updates, including paused frames. Its input view is borrowed only during
the callback. Publish there so all script calls in a frame see the same data. An
error, exception or stop prevents later frame callbacks; quit skips preparation.
Enable Unicode text delivery through the existing Window.setTextInput(true) API.

```cpp
import Dot.Script.InputSource;
import Dot.Script.LiveScripts;
import Dot.Core.Application;

// In a Result-returning application owner scope:
auto input = Dot::Script::InputSource::create();

if (!input) {
  return std::unexpected(input.error());
}

auto services = input->bindings();

if (!services) {
  return std::unexpected(services.error());
}

Dot::Script::LiveScriptsConfig config;
config.scripts.services = std::move(*services);
auto initial = input->publish(application.input());

if (!initial) {
  return std::unexpected(initial.error());
}

Dot::Core::LoopCallbacks callbacks;
callbacks.prepareFrame = [&](const auto& state) -> Dot::Core::Result<void> {
  return input->publish(state).transform([](const auto&) {});
};
```

The application should check all expected results before continuing. Successful
publication increments a nonzero uint64 index, sent as a decimal string. Coordinates
must be finite; text must be scalar UTF-8 within a configured byte limit (default
64 KiB, range 1 byte..1 MiB). Failed publication retains the previous frame/index.
Native host transport budgets still apply: large custom text limits may require a
larger Runtime JSON budget, especially when control characters need escaping.
Frame reads before publication fail Unavailable; textInputRequested can be read
before the first publication. A missing input service fails
NotFound through invoke, including with an entirely empty callback registry.

| Native method | Arguments | Owned result |
| --- | --- | --- |
| `input.snapshot` | `[]` | complete InputFrame |
| `input.key` | `[Key name]` | `{down, pressed, released}` |
| `input.button` | `[MouseButton name]` | `{down, pressed, released}` |
| `input.pointer` | `[]` | `{x,y}` position in window coordinates |
| `input.pointerDelta` | `[]` | `{x,y}` accumulated frame movement |
| `input.wheel` | `[]` | `{x,y}` accumulated frame scroll |
| `input.text` | `[]` | frame text |
| `input.index` | `[]` | publication index string |
| `input.pointerCaptured` | `[]` | last native capture acknowledgement |
| `input.capturePointer` | `[boolean]` | request relative pointer capture; no return data |
| `input.requestTextInput` | `[boolean]` | independent text/IME request; no return data |
| `input.textInputRequested` | `[]` | current script request, initially false |
| `input.textInputActive` | `[]` | last actual native text/IME acknowledgement |

The capture request does not operate a window or forge acknowledgement. The native
frontend reads `pointerCaptureRequested()`, applies its focus/input policy through
SDL, and passes the resulting state to `publish(input, pointerCaptured)`. Headless
publication defaults to false. Requests survive script realm reloads. Reads remain
observational; capture requests are ReadWrite and forbidden during component
preparation and state export/import. A failed publication preserves the old frame
and acknowledgement. Native mode transitions clear only accumulated pointer delta,
preserving other current-frame input. Consume mouse motion once per variable frame,
because multiple fixed steps can share the same publication.
Text input is explicit: scripts call `input.requestTextInput(true)` for text entry
and false when returning to key-driven UI/gameplay. Requests default false, survive
realm reload and are ReadWrite operations denied during preparation. The native
player enables SDL text mode only when requested and relative capture is inactive.
The editor uses the game request during Game input capture and restores its own
text mode for editor fields outside capture. Neither frontend changes the system
input method. Native publication includes the actual `textInputActive` flag;
headless callers default it false. HUD navigation/confirmation respects the game
request independently of an editor field's actual text mode. No Unicode text events
are produced while native text input is suspended.

The SDK exports frozen `keys` and `mouseButtons` name arrays, matching every
supported native enum except Unknown/Count. Key names include A..Z, Digit0..9,
Space/Enter, arrows, Delete/Insert/Home/End/PageUp/PageDown/KeypadEnter, F1..F12
and left/right modifiers; mouse names are
Left/Middle/Right/Back/Forward. Unsupported names, numbers, case changes and invalid
argument shapes fail explicitly. Native callback arguments are bounded to 4096
bytes/depth 8. Reads do not consume edges, motion, scroll or text. Fixed updates
sharing one publication therefore observe the same pressed/released values; games
must place one-time edge actions in their chosen frame/update policy. Repeat events
hold a key without adding a press. Focus loss releases held keys/buttons. Editing
returned snapshots never changes input or the native InputState.

```ts
import {ScriptComponent, input} from 'dot:engine';

export class Mover extends ScriptComponent {
  onUpdate(seconds: number): void {
    const transform = this.self.transform;
    transform.position.x += (input.key('D').down ? seconds : 0);
    this.self.transform = transform;
  }
}
```

`HostFunction.access` is trusted native metadata: ReadOnly methods promise
observational behavior, while the default ReadWrite methods are denied during
component preparation and state export/import. The VM does not verify native
implementations or apply this scheduling policy to a direct Runtime. SceneHost
marks its reads explicitly; InputSource marks nine queries ReadOnly and the capture
request ReadWrite. Additional
services cannot override scene methods, must have canonical unique names, a valid
classification and nonempty callbacks, and share the total 256-method limit.
SceneScripts retains original callbacks/dependencies in its configuration and
rebuilds guards for each candidate realm; LiveScripts carries the same services
through compilation failures and successful reloads. Capture owned dependencies,
or keep borrowed dependencies alive for the service lifetime.

# Runtime physics services

`Dot.Script.PhysicsSource::create(sharedScene, config)` owns a real Jolt-backed
SceneSimulation, the original shared scene identity, and a published contact batch.
Add its bindings to SceneScriptsConfig.services or LiveScriptsConfig.scripts.services.
Moves preserve callbacks, which retain the scene/simulation after the publisher
handle dies. Moving/replacing the captured Scene invalidates the source explicitly.
Operations require the creating thread; destruction after quiescence may occur
elsewhere. No script call creates, advances or resets a simulation implicitly.

Native scheduling calls synchronize(), step(seconds), or reset(). step uses the
existing scene simulation's synchronization, integration and atomic pose writeback;
seconds must be finite in [0.000001,0.25]. Queries show last synchronized runtime
state, separate from the authored Entity.rigidBody description. New/removed/edited
components take effect on native synchronization/step. Runtime controls apply to
synchronized bodies; an unsynchronized new body fails NotFound instead of creating
one implicitly. Missing/foreign entity owners fail immediately. A ray hit on a
removed owner fails InvalidState and requires synchronization instead of becoming
a false miss. Pose changes can be authored through Entity.transform and applied by
native synchronization; runtime velocity is retained when initial velocity is unchanged.

| Native method | Arguments | Result |
| --- | --- | --- |
| `physics.body` | `[entityId]` | owned PhysicsBodyState |
| `physics.character` | `[entityId]` | owned CharacterState |
| `physics.stats` | `[]` | native counters, fault state and generation string |
| `physics.rayCast` | `[origin, displacement]` | owned RayHit or null |
| `physics.sphereCast` | `[origin, radius, displacement, {includeSensors, ignoreEntity}]` | owned SphereHit or null |
| `physics.contacts` | `[]` | owned ContactBatch; reads do not consume |
| `physics.setVelocity` | `[entityId, velocity]` | undefined |
| `physics.setAngularVelocity` | `[entityId, angularVelocity]` | undefined |
| `physics.addImpulse` | `[entityId, impulse]` | undefined |
| `physics.moveKinematic` | `[entityId, position, quaternion, seconds]` | undefined |
| `physics.setCharacterInput` | `[entityId, {velocity, jump}]` | undefined |
| `physics.setCharacterYaw` | `[entityId, degrees]` | undefined |
| `physics.teleportCharacter` | `[entityId, {position, yawDegrees}]` | undefined |

RigidBody authoring includes `collisionDetection: 'discrete'|'continuous'`; the
factory defaults to discrete. Continuous requires solid Dynamic motion. Native
PhysicsBodyState reports the applied backend mode using the same field. Changing
only this authoring property preserves current native linear/angular motion on
synchronization; malformed/unsupported combinations fail before replacement.

Ray/sphere hits, native proxy manifolds and virtual contact events include character-controller
entities. Sphere filters use `ignoreEntity` for either synchronized collider kind;
a following camera can exclude its owner's capsule. Native
support identity maps both rigid bodies and live peer characters to groundEntity.
Rigid-body mutation methods still reject character entities.

Vectors/quaternions use exact {x,y,z}/{x,y,z,w} fields with finite float-range
numbers. IDs are canonical positive decimal uint64 strings. Arguments reject
extra/missing fields, duplicates, invalid scalar types, raw NUL/invalid UTF-8,
more than 64 KiB or nesting beyond 16. Native motion/range/shape rules are unchanged:
velocity <=500 m/s, angular speed <=15*pi rad/s, impulses only for dynamic bodies,
and kinematic targets only for kinematic bodies with bounded duration/speeds.
Rays use displacement rather than an unbounded direction; length is at least
0.001 and both endpoints must fit native position limits. Sensors are included;
an inside hit may have fraction zero and null normal. Returned poses, velocities,
ray geometry and contact geometry are world-space metres/seconds.

`physics.sphereCast(origin, radius, displacement, filter = {})` provides a native
volume sweep for camera/gameplay queries. The wrapper defaults to excluding sensors
and no ignored body. SphereHit extends RayHit with center and penetrationDepth;
point is on the obstacle and center is the sphere center at the reported contact.
Initial overlaps return fraction zero without automatic correction. Filtering,
range and lifecycle contracts follow [native sweeps](Physics.md). This is ReadOnly
during component preparation; it does not allocate a gameplay body or step physics.

```ts
import {ScriptComponent, physics} from 'dot:engine';

export class Player extends ScriptComponent {
  onStart(): void {
    this.self.setVelocity({x: 1, y: 0, z: 0});
    this.self.addImpulse({x: 0, y: 2, z: 0});
  }

  onUpdate(): void {
    const body = this.self.physicsBody;
    const hit = physics.rayCast(body.position, {x: 0, y: -2, z: 0});

    if (hit !== null) {
      this.self.name = 'Hit ' + hit.entity.value;
    }
  }
}
```

Entity.physicsBody and the Entity setVelocity/setAngularVelocity/addImpulse/
moveKinematic methods delegate to the same global physics service. All queries
are ReadOnly and may run in constructors/state migration; controls are ReadWrite
and denied during preparation/export/import. Hot reload keeps the same native
simulation and motion through retained service captures. Reset is a native action:
it prepares a replacement from authored state, increments generation, and publishes
an empty batch without replacing callbacks; ordinary preparation failure retains
old state. Body/stats queries remain diagnostic after a contact-publication fault.

Each successful synchronize/step drains native events once into a batch for all
script readers. Batch indices and native step/generation values are decimal strings.
Observed Begin/Stay events include discriminated geometry. `kind: 'manifold'`
provides `normal`, `penetrationDepth` and paired `points` (negative depth denotes
speculation). `kind: 'character'` provides `normal`, counterpart-surface `point`,
`separation` and `characterIsFirst`. Signed separation includes controller padding;
solved predictive character constraints can have positive separation. Both normals
follow event pair order. Narrow `geometry.kind` before reading type-specific fields.
Lifecycle events can omit geometry. End reason is null except on End
(separated, poseChanged, destroyed, reconfigured). Removed entity IDs remain valid
historical data and may be stale in the scene. Reads return independent JSON
snapshots, so multiple components see the same events even if one edits its copy.
The application owns publication/dispatch timing; later publications replace the
previous batch. Games should process each chosen batch index once.

Contact JSON budget defaults to 1 MiB (1 byte..16 MiB, but must hold an empty batch)
and must fit the receiving Runtime's transport. A native mutation may succeed
before event capture/serialization fails or exceeds this budget; this faults the
source and requires reset, while contact reads reject the stale batch. Faults are
reported explicitly and do not roll back completed physical advancement. Reset
prepares its empty encoded batch before replacing the world. A Runtime rejecting
an otherwise valid published batch does not consume native data; another reader
with adequate transport capacity can still read it.

# Character authoring and movement

`dot:engine` exports CharacterController, CharacterInput and CharacterState.
`components.character()` creates the native defaults. Entity.character reads/writes
the saved component through the same validated scene service as editor/agent edits;
it is mutually exclusive with Entity.rigidBody. State queries are owned snapshots,
not live mutable handles. Large/noninteger maxContacts values are rejected before
conversion. Changed descriptions bind at the next native physics synchronization.

```ts
import {input, ScriptComponent} from 'dot:engine';

export class Walker extends ScriptComponent {
  onFixedUpdate(): void {
    this.self.moveCharacter({x: input.key('D').down ? 3 : 0, y: 0, z: 0},
        input.key('Space').pressed);
  }
}
```

Entity.characterState and physics.character(id) return world feet/rotation/velocity,
ground state/normal/velocity and optional scene-scoped groundEntity. PhysicsStats
counts characters independently from bodies. Entity.moveCharacter and
physics.setCharacterInput supply persistent world horizontal velocity plus a jump
edge consumed on the next substep, including in air. Entity.setCharacterYaw and
physics.setCharacterYaw turn without clearing motion. Entity.teleportCharacter and
physics.teleportCharacter(id, position, yawDegrees) immediately place both the
native character and scene pose, clear velocity/pending input and publish a new
contact batch without advancing time. Same-pose calls still clear motion. Character
and ancestor authoring edits must first be synchronized by the native scheduler;
unsynchronized edits fail without replacing them. Invalid inputs preserve pose and
intent; native/writeback/contact-publication failures after mutation can fault the
service, requiring native reset. Initial overlap resolves in the next step.
Native failures throw HostError. Queries are ReadOnly during lifecycle preparation/migration; all controls
and component assignments are ReadWrite and denied in those phases. Scripts cannot
advance/reset the simulation. Capsule constraints and native collision limitations
are documented in [physics](Physics.md).

The [Feature Lab](../examples/FeatureLab/README.md) includes an ordinary saved
controller and arrow/J script. Its tests exercise actual input, native jumping,
changed-source movement speed, retained physics through hot reload and landing
without any desktop window or scripted substitute for native physics.

# Runtime audio services

`Dot.Script.AudioSession::create(sharedScene, files, mixerConfig, limits)` owns a
real offline SceneAudio backend. createPlayback uses the same scene/clip binding
with a real device/worker and returns explicit failure if no endpoint can open;
there is no silent-device fallback. Add the session bindings to the existing
SceneScriptsConfig/LiveScriptsConfig native services. Moves preserve callbacks,
which retain scene/clips/backend ownership. Releasing the last owner joins native
playback and releases its graph/cache; quiescent destruction may occur elsewhere.
Moving/replacing the captured Scene invalidates the session. Calls require the
creating client thread.

Native scheduling synchronizes authored paths/settings/world poses through the
existing prepared batch. Unchanged descriptions preserve cursor/manual play state;
new or edited components take effect on synchronize. Spatial sources require mono
clips and a primary listener, with existing pose/velocity constraints. Reset reloads
assets and authored play state, increments generation and restores master gain to
1 without replacing callbacks. Preparation failures preserve old voices, gain and
generation. Native playback runs continuously; offline render is a separate native
operation. Scripts cannot open, advance, reset or destroy backends.

| Native method | Arguments | Owned result |
| --- | --- | --- |
| `audio.voice` | `[entityId]` | AudioVoiceState |
| `audio.stats` | `[]` | counts/mode/gain/generation and optional native output statistics |
| `audio.listener` | `[]` | world-space position/rotation/velocity |
| `audio.play` | `[entityId]` | undefined |
| `audio.pause` | `[entityId]` | undefined |
| `audio.stop` | `[entityId]` | undefined |
| `audio.seek` | `[entityId, sourceFrameString]` | undefined |
| `audio.setVolume` | `[volume]` | undefined |
| `audio.resetMotion` | `[]` | undefined; request the next pose baseline |

Entity.audioVoice and playAudio/pauseAudio/stopAudio/seekAudio delegate to these
same methods. Missing/foreign owners and unsynchronized voices fail explicitly.
Queries reflect synchronized native data, and returned snapshots are owned.
Frame counts/cursors and lifetime counters cross JSON as canonical unsigned
uint64 decimal strings, including zero; leading zeros, signs, fractions and numeric
JS frame arguments are rejected. Seek requires [0,clip.frames) and never starts
playback implicitly. Native callback bytes are bounded to 64 KiB/depth 16 with
strict UTF-8/JSON/duplicate/object-shape checks. Gain is finite [0,1].

```ts
import {ScriptComponent, audio} from 'dot:engine';

export class Music extends ScriptComponent {
  onStart(): void {
    this.self.seekAudio('0');
    audio.setVolume(0.5);
    this.self.playAudio();
  }

  onStop(): void {
    this.self.stopAudio();
  }
}
```

Pause retains the source cursor. Stop pauses and rewinds to zero without graph
advancement, using one worker command in native mode; a backend failure after
partial stop explicitly faults the mixer. Play resumes a paused voice or restarts
an ended voice. VoiceState.sourceFrame is the backend **source read cursor**, which
may include prefetch/resampling; it is not an audible presentation timestamp.
Driver/queue PCM retains already mixed content: all play/seek/gain/settings controls
affect subsequent mixed blocks, and do not alter system/global volume. Per-source
settings use the existing authored audioSource description and native synchronization;
audio.setVolume controls the owned mixer gain independently of endpoint gain.

Voice/listener/stats reads are ReadOnly and may run during constructors and state
migration. The six controls are ReadWrite and denied during preparation/export/
import. Hot reload keeps the same native voices/backend; paused cursor and gain
remain unchanged while script private state can migrate. In native mode, mixing
continues during compilation/preparation, so snapshots can advance independently.
Stats expose actual playback state/errors and queue/frame/callback counters; offline
mode explicitly reports null playback data.

## Published ESM generations

`CompiledProgram::publish` writes JS/MJS and a strict version-1 manifest to a
new directory without replacing existing destinations. It strips trailing source
map directives and validates against default loader limits before publication.
`loadPublished` validates portable sorted names, directory case aliases, exact
membership/counts/sizes and UTF-8/NUL boundaries, then captures ESM into a private
owned generation without compiler processes. Keep an owner alive for Runtime
using its borrowed files. Subsequent package changes do not affect captured code.

## Animation service

Import `animation` and `AnimationState` from `dot:engine`. The native service
controls saved scene players using owned IDs and clip library indices, with
play/resume/restart, pause, stop, second-based seek and speed/loop configuration.
Entity convenience methods delegate to the same service; controls remain transient
and leave the authoring description unchanged. Constructors and reload migration
may read `animation.state` but cannot issue controls. Foreign/stale/disabled/missing
players and invalid values produce catchable DotHostError values; scripts cannot
advance the native clock or create a second backend. See [Animation](Animation.md)
for exact transaction, timing, stop/restoration and clip-selection semantics.
Saved controllers additionally accept typed `animation.parameters` / Entity parameter
updates and explicit reset. They own base selection/speed/loop; conflicting manual
controls reject. Native controller snapshots, clocks and fades survive source reload;
transition counts are decimal strings. See the saved-state-machine section there.

GameSession establishes a zero-duration audio pose baseline after the first Start
callbacks and animation sampling. Initial spawn/listener placement is not inferred
as high-speed motion. Repeating Start does not reset an established audio velocity
sample. Subsequent variable updates use their actual elapsed duration.
For intentional teleports/origin shifts, `audio.resetMotion()` requests a new pose
baseline at the next successful native synchronization. All source/listener derived
velocities are zero for that synchronization; subsequent samples use ordinary
Doppler again. This explicit game-wide request preserves voices, cursors, gain and
generation, and does not change queued PCM or reset/heal the backend. Stats report
`motionResetPending` until successful synchronization; failed or invalid syncs retain
the request, and a successful full native reset clears it. Ordinary unmarked
out-of-range velocities remain errors. Callers can request this after character
placement so a separately following listener is not inferred as extreme motion.

Native GameSession audio remains `prepared` during script construction and Start:
autoplay/play controls establish voice state without advancing source frames or
starting the device. After initial animation and audio synchronization succeed,
GameSession activates playback once. `audio.stats().playback.state` then becomes
`running`; native output failures remain real errors. Scripts cannot start the
endpoint. Offline deterministic rendering retains its existing explicit scheduling.


## Scene replacement

GameSession (editor Play, project player and compiled game) supplies `scenes` through
`dot:engine`. `scenes.current()` returns the calling realm's `{scope,path}`; path is
null only for a cloned authoring scene whose native caller supplied no origin.
Preparation reads identify the candidate realm, not the still-running old scene.
`scenes.request(path)` queues one portable project-relative `.dotscene` path and
returns a canonical decimal request ID. No loading occurs inside that host callback.
Constructors/reload migration cannot request a cut. A stale/retired realm or another
pending/loading request produces catchable DotHostError.

```ts
import {input, scenes, ScriptComponent} from 'dot:engine';

export class Door extends ScriptComponent {
  onUpdate(): void {
    if (input.key('L').pressed) {
      scenes.request('scenes/next.dotscene');
    }
  }
}
```

`scenes.status(id)` returns owned `{id,path,kind,root,state,committed,error}`. IDs are local to
the GameSession, monotonic across its cuts, with the last 16 retained. Unknown or
expired IDs fail; errors use stable HostErrorCode names. Acceptance is pending, not
success. Preparation failures keep the current world running; check the result to
handle retry/UI. A committed startup failure retains the new scene for inspection
and faults the session. Closing/faulting cancels uncommitted requests. A fresh scene
uses fresh services/input and reruns Start; an existing shared native scene reference
still owns its old scene but is no longer active. A cloned authoring session with an
origin path retains that initial scene snapshot (at most the scene codec's 64 MiB
limit), so returns to the origin preserve its unsaved starting content. Runtime scene
mutations do not change this seed; other target paths read their saved files. Normal
project sessions read saved scenes and compiled packages read their owned data.
Scene-specific TypeScript globals
and component state do not transfer. Hot reload disabled means the existing compiled
generation is reused across cuts. Compiled packages never invoke a compiler.

The variable update processes at most one request after current callbacks, animation
and audio. Fixed steps do not cut, invalid deltas have no effects, and Start may queue
another request for the next update. Native playback is staged silently until Start
and pose synchronization; graphics data preflight validates actual assets, normals/
normal-map frames, image encoding and HDR input without creating a device. Preflight
bounds unique graphics inputs to 32768 and decoded mesh/image data to 512 MiB; native
subsystems retain their own configured limits. The operation is synchronous; GPU
allocation/render failures can still happen after commit. There is no additive scene
composition, background streaming or persistent gameplay-state transfer in this API.

## Game state and save slots

The engine-owned module exports `gameState` and `saves`; see
[game storage](GameState.md#script-api) for the strict JSON, revision and lifetime
contracts. GameSession supplies the shared services across scene replacements.
Constructors/migration only observe in-memory state; all disk access requires an
active lifecycle phase. No implicit save, load, scene switch or entity restoration
is performed. The native player uses stable application identity/user directories;
editor Play uses isolated disposable storage.


## Hierarchy operations

The built-in `dot:engine` module shares native hierarchy implementation with the
editor. `Entity.parent = value` preserves local TRS; `reparent(parent, 'world')`
preserves the world matrix explicitly. The alternative space is `'local'`. For
example, inside a lifecycle callback:

```ts
import {scene} from 'dot:engine';

const a = scene.create('A');
const b = scene.create('B');
a.reparent(b, 'world');
const assembly = scene.group([a, b], 'Assembly');
const exposed = scene.ungroup([assembly]);
```

Group accepts 1..10000 unique live entities, groups minimal selected roots at the
last entity's world origin, and returns the new container. Ungroup accepts plain
containers, preserving surviving poses and returning affected children sorted by
ID. It rejects functional components or nodes referenced by native skin/animation
bindings. Unrepresentable shear, foreign/stale/duplicate handles and invalid
hierarchies produce catchable host errors without partial native scene changes.
These three operations mutate native state and are unavailable during read-only
constructor/reload preparation. Surviving handles keep their scope; removed IDs
stay stale. See [native contracts](Scenes.md#native-hierarchy-editing) for limits
and shared-parent behavior. Grouping does not provide linked instances or prefabs.


## Native game text

`dot:engine` supplies the scene-local `hud` service: `label`, `snapshot`, `set`,
`remove` and `clear`. Labels use normalized viewport rectangles and native text
rasterization/composition in both Play and exported games. Constructors/migration
may inspect but cannot mutate them. Scene replacement starts a fresh HUD; game-wide
save data is independent. See [text, fonts, limits and product controls](GameUi.md).


## Unscaled frame callbacks and pause

`ScriptComponent.onFrame(seconds)` runs before variable gameplay, even when native
simulation is paused. Use it for menu input and HUD changes. `gameClock.snapshot()`
and `gameClock.setPaused(boolean)` share the strict scene-local native service.
While paused, `gameClock.step()` queues one native fixed/update/physics/animation
quantum at 1/fixedHz and returns its request ID. Inspect snapshot().step for actual
completion/cancellation/failure; this does not unpause or advance audio by simulation time.
Paused fixed calls skip game callbacks/physics; paused variable calls skip Update
and advance animation by zero. Audio, input, rendering, reload and explicit scene
requests continue. See [phase ordering, ownership and product controls](GameClock.md).


## Additive runtime scene content

`scenes.append(path)` queues saved materialized `.dotscene` entities under a new
identity-transform container in the current world. It shares the replacement
queue and request history; inspect `scenes.status(id)` until terminal. `kind` is
`replace`, `append` or `remove`; a published append returns a scoped `root`.
`scenes.instances()` returns the active `{root,path}` list independently of the
last 16 request records, with at most 64 live instances. Duplicate source paths
produce independent IDs. Replacement clears this ownership list; stale roots fail.

The source's environment, AO, UI and authoring links are stripped before CPU
resource preflight. Native skin/animation references remap, primary cameras and
listeners are demoted, and opaque script JSON remains literal. Existing world
scope, entity handles, script objects, clock, physics motion and animation state
remain active; only newly attached components receive Start. Content is read from
the saved file even when its path matches an unsaved Play origin.

`scenes.remove(root)` queues removal of the original member list, including its
container. Reparenting members does not transfer ownership; already destroyed
members are tolerated and their values never reused. Surviving external children,
skin joints or animation targets into those members reject removal before mutation.
New script-spawned children are external to this list: remove/reparent them first.
Script-property JSON is not interpreted as native references.

Preflight or ownership failure leaves the active world running. After publication,
native synchronization or component construction/Start/Stop failure reports
`committed:true` and faults the session; completed callbacks cannot be rolled back.
Successful composition synchronizes existing services without replacing the realm
or rebuilding physics. This is functional additive composition, not asynchronous
GPU residency. The content worker owns decoded animation clips/controllers and
collision geometry and decoded PCM under the session's exact audio budgets; the
first native synchronization consumes these without file fallback for new bindings. Unchanged animation players retain clocks/rest poses;
existing collision paths retain immutable cached geometry until reset. New paths
share the 64-asset/64 MiB collision cache bound. Missing/duplicate/unused prepared
resources fail explicitly. Preparation entries serve this publication; services
retain owned immutable handles as needed, while later description replacements use
ordinary refresh rules. A second synchronization
after Start handles script-authored changes normally.

Fragment grafting, native binding and later dynamic asset synchronization still run
on the owner; audio decoding without a prepared proof and script-authored file
changes retain ordinary synchronous behavior. GPU uploads remain independent work. Asset edits must be
serialized through publication, as for scene replacement.

追加/卸载共享帧边界队列，接受请求不代表完成。实例保留精确成员所有权，来源全局设置与
编辑链接不导入；已有玩家、脚本、物理及动画继续运行。发布前失败保留游戏，发布后同步或
钩子失败标记已提交并使会话故障；尚不代表完整后台资源流送或 GPU 驻留。


Runtime insertion prepares only the incoming descriptions; it does not serialize,
decode or replace the existing world. New private component rows are staged on the
owner, grafted world matrices/camera inverses are checked, and public allocation
advances only at successful publication. Failure reclaims every staged row and its
new cached transform; existing values, descriptions and globals persist. Private
container capacity may grow during a rejected transaction and remains owned until
scene destruction. No scripts, devices or file writes run inside the World
transaction. The live scene's 10000-entity bound still includes the container;
file-codec byte limits apply when encoding saved/output scenes, rather than requiring
an otherwise unrelated serialization of the active world on every append.

运行时追加只准备新内容，拥有线程在私有注册表中暂存组件并校验挂接世界变换/相机逆矩阵，
成功后才推进公共 ID。失败回收新实体及其缓存，已有描述/全局数据不重建；容器容量仍由
场景拥有并在销毁时释放。这一步不执行脚本、设备或文件写入，仍保留 10000 实体上限。


## Native navigation / 原生导航

`navigation.stats()` reports the baked surface or null; `navigation.route()` returns a
complete static route from actual native character feet. `move(entity, goal, speed,
options)` delegates corner following and local reciprocal velocity avoidance to native
code. `state()` observes moving/arrived/failed, actual distance, blocked state and
neighbors; `agents()` lists controlled characters, `stop()` releases velocity ownership.
Jolt owns actual poses and grounded arrival. Pause/single-step share game scheduling;
reload retains native movement and permits observational state reads during migration.
See [navigation contracts](Navigation.md) for strict editor/player operations, budgets,
profile/geometry checks and limitations. Feature Lab covers unbound reads,
idempotent stop and catchable unavailable move/route errors.
