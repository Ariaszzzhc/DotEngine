# ESM runtime

`Dot.Script.Runtime` owns one QuickJS-NG 0.16.2 realm/context and a FileSystem
copy. Vendor types stay in the implementation unit. The library is a private
pinned vcpkg dependency; its libc/OS modules are not linked into the engine.
The API follows the [official embedding API](https://quickjs-ng.github.io/quickjs/developer-guide/intro/).
The [native TypeScript compiler](#native-typescript-compilation) produces ESM for
this layer. The [registered host bridge and core scene SDK](#registered-host-services)
are implemented; component lifecycles, hot reload and remaining subsystem APIs
are still to be implemented.

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
entity-attached component instances, lifecycle scheduling and hot reload are not
implemented yet. Candidate load/initializer failures can have native side effects,
so this direct bridge must not be mistaken for transactional hot reload.

Run `DotRuntime --script-host-test <absolute-compiler> <sdk-root> <directory>` for
the actual TypeScript SDK, native entity creation/deletion and parenting, renaming,
recoverable transform rejection, sixty typed transform ticks, and compiler/process/
VM cleanup. `<sdk-root>` contains `dot/engine.ts` and `dot/host.d.ts`.
`--script-host-render-test` takes the same arguments and also checks actual GPU
pixels before/after script-driven movement of a native cube. macOS uses Metal;
Windows/Linux implementations are not validated locally. No sanitizers are used.
