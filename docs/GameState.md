# Gameplay state and save storage

`Dot.Game.State` and `Dot.Game.Saves` provide native storage. GameSession owns one
`Dot.Script.GameData` service across scene replacements, exposed through the
engine-owned `dot:engine` SDK and the editor's shared product operations. Native
callers can use the typed storage interfaces or GameData's strict JSON protocol.
No save/load is implicit; a game chooses when to persist or restore progress.

## In-memory state

`State::create` accepts a JSON root object and starts at revision 1. Copies share
the same store; snapshots and `get` results own their bytes. Calls require the
creating thread, including after moves. Quiescent destruction may occur elsewhere.

- Keys are literal UTF-8 names of 1..128 bytes, without ASCII control characters
  or DEL. A slash does not make a key a path. A missing value returns `nullopt`,
  distinct from the encoded JSON value `null`.
- Input rejects duplicate JSON fields, invalid scalar Unicode, non-finite numbers
  and numbers outside JavaScript's safe-integer magnitude. Large counters and IDs
  must be strings. Nested objects and arrays are supported, with depth bounded at
  32. Root entry limits apply independently of the bounded byte payload.
- Defaults allow 256 KiB and 256 root entries. Configurable limits are 2 bytes to
  1 MiB and 1..4096 entries. Both input and compact normalized output must fit.
- `set`, `remove` and `replace` validate/allocate before publication. Failure leaves
  data and revision unchanged. Semantic no-ops retain the revision; successful
  changes increment it. Revisions never wrap.
- `replace(data, expectedRevision)` requires the current store revision. A snapshot
  from another store or disk is data provenance, not a revision to assign directly.

## Save slots

`Saves::open` accepts an existing game-owned `Core::FileSystem` root. It does not
create that directory or implicitly choose a project/installation path. The caller
must serialize filesystem edits; this is not protection against hostile concurrent
filesystem changes, and save contents are not authenticated.

Slot names contain 1..64 lowercase ASCII letters, digits, underscores or hyphens.
Portable reserved device names are rejected. Each slot is one flat
`<slot>.dotsave` file. Managed entries must have canonical spelling and be regular
files: aliases, symlinks, directories and special files fail explicitly. Unrelated
extensions are ignored. Enumeration is bounded to 4096 directory entries.

The strict envelope contains exactly:

```json
{"format":"dot-save","slot":"checkpoint","revision":"3","data":{"visits":2}}
```

Revision is a canonical nonzero decimal string below the maximum uint64 value.
Data obeys State's JSON contract. Unknown/missing/duplicate fields, mismatched slot
identity, oversized or damaged content fail reading. `read` returns an owned
snapshot and never changes a live State. Apply a successful read through `replace`
with the current live revision; stale edits are rejected instead of overwritten.

`write(slot, snapshot, overwrite)` requires explicit permission to replace an
existing slot. It prepares all payload/result allocations before writing a flushed
sibling temporary file and atomically replacing the destination through
`FileSystem`. Failed writes preserve existing slot bytes under the serialized
filesystem contract. Directory crash durability is not promised.

The default capacity is 256 slots, configurable to 1..1024. Capacity limits new
slots; reducing it does not prevent listing, reading, overwriting or removing
existing slots. Listing reports names and byte sizes without parsing contents, so
damaged or oversized saves remain discoverable and removable. Removing a missing
slot fails. Losing the root fails subsequent operations without recreating it.

## Application identity and directories

Project metadata requires `applicationId`, a stable 1..64-byte lowercase ASCII
letter/digit/underscore/hyphen literal excluding reserved device names. New blank
projects generate an identity when creation input leaves it empty; encoding/opening
existing metadata requires it. New template instances generate independent IDs.
Rename, move and export retain the stored ID. Display names never determine the
save namespace. Manually duplicating an app ID intentionally shares that namespace.

The native player resolves the user/app directory through
[SDL_GetPrefPath](https://wiki.libsdl.org/SDL3/SDL_GetPrefPath), with organization
`DotEngine` and the stable ID, and uses its `saves` child. Errors propagate; there is
no project/install-directory fallback. This platform operation needs no video/audio
initialization. An explicit native-player `--data-directory` uses an existing
caller-owned app root and its `saves` child instead; missing explicit roots fail,
without falling back. This is intended for controlled development/portable ownership,
not a change to ordinary application-ID storage. GameSession itself only uses an
explicitly supplied root and never creates user directories implicitly.

Editor Play always owns a fresh temporary directory and fresh state. Stop explicitly
removes owned files even when Stop hooks fail. Cleanup failures report the path and
retain ownership for retry; restarting Play retries cleanup before creating another
store. Destruction/failed initial preparation perform best-effort RAII cleanup. A configured temporary parent still receives an owned
child, never direct writes. Passing a player save root to Play fails. Project and
installation files and real player progress remain separate. A retained game-data
handle permits in-memory snapshot/get after retirement; new work and live-state writes
fail after close, fault or destruction; retained receipts remain memory observations.

## Background jobs and script API

The native `Saves` file codec remains a synchronous tool/backend interface.
`Dot.Game.SaveQueue` owns one FIFO worker and is the game service's disk path.
Creating it, submitting a snapshot, status, take and cancellation perform no
owner-thread filesystem operations. There is no synchronous script fallback.
Accepted snapshots own their bytes independently of later game-state changes.
The worker creates its own thread-bound save store and serializes disk operations.
Failed initial root access can retry on a later job; no root is recreated implicitly.

```ts
import {gameState, saves, type SaveTicket} from 'dot:engine';

let pending: SaveTicket<'write'>|null = null;

function requestCheckpoint(): void {
  pending = saves.write('checkpoint', false);
}

function pollCheckpoint(): void {
  if (pending === null) return;
  const receipt = saves.take(pending);
  if (receipt.state === 'pending' || receipt.state === 'running') return;
  pending = null;
  if (receipt.state === 'failed') throw Error(receipt.error.message);
  if (receipt.state === 'cancelled') return;
  // This terminal receipt, rather than submission, proves successful persistence.
  const storedName = receipt.result.name;
}
```

`list/read/write/load/remove` return typed `{id, operation}` tickets. Poll/take them
from later frames; unresolved promises are not introduced into the existing bounded
script-hook scheduler. `status(ticket)` and `jobs()` observe memory without results.
`take(ticket)` returns a pending/running observation, or consumes a terminal receipt
exactly once. Successful terminal receipts include the operation's typed `result`;
failed ones include code/message, cancelled ones explicitly state cancellation.
Unknown/consumed IDs and mismatched operation kinds fail. IDs are unique in a process,
including across stores, and are never reused. Retain tickets in component migration
state when work must survive script reload; the game-wide queue survives scene changes.

A write captures the current game state unless an explicit owned `GameStateSnapshot`
is provided as its third argument. This allows a game to construct a checkpoint in
an independent snapshot and update its live checkpoint key only after persistence,
without temporarily changing/rolling back unrelated live state. Saved revision is
provenance. A read returns an owned snapshot without applying it. A load requires
the current revision on submission, reads in the worker, then reports `ready` until
its terminal take. Only that take applies data, checking the original expected
revision again; intervening edits fail without publishing stale state. Loading
still replaces data, not a scene, character pose, physics world or animation clocks.

Cancel only pending work; a worker that has already started a disk operation must
finish, and cancellation fails rather than implying rollback. Default limits are
32 outstanding jobs (including terminal receipts) and 4 MiB of payload reservations;
configurable ranges are 1..256 and 1 KiB..64 MiB. Reads reserve the configured state
limit, listings reserve 4096 bounded slot names. Typed bookkeeping and bounded codec
scratch are separate from payload accounting. Take completed receipts to recover
capacity; queues never grow unbounded or silently evict results.

Only in-memory snapshot/get and job status/list are read-only preparation services.
Constructors and migration cannot submit disk work, take results, mutate state or
cancel operations. Once started, Stop hooks can enqueue a final save. Normal shutdown
stops submissions, drains accepted operations and joins before Play deletes its
owned save directory. It may wait for the disk; asynchronous retirement and crash
recovery are not claimed. Unconsumed failed writes/deletions make explicit close
fail with context. Quiescent retirement may run on another thread; ordinary operations remain on the
creator. After retirement, native retained service handles permit final memory observations and receipt collection; taking a load cannot apply retired data.

Feature Lab polls camera checkpoints on its unscaled frame hook; its SDK auditor
sequences real file jobs over frames, handles legitimate revision
conflicts, and deletes its own demonstration slot without overwriting intervening
live data.

## Product protocol and editor

Window → Game state shows current Play data, revision and save slots, with Save,
Load, Delete and Refresh controls. It states that Play saves are temporary. The same
implementation serves native GameData::perform and the live workspace:

```json
{"command":"game-data","session":"1","value":{"command":"snapshot"}}
{"command":"game-data","session":"1","value":{"command":"set","key":"score","value":7}}
{"command":"game-data","session":"1","value":{"command":"save","slot":"checkpoint","overwrite":false}}
{"command":"game-data","session":"1","value":{"command":"load","slot":"checkpoint","revision":"2"}}
```

The opt-in standalone player exposes the same inner operations through
`game-data(value)`; see [native product control](Export.md#opt-in-native-player-control).
Its explicit app data directory persists across process restarts. Editor Play keeps
its isolated temporary storage. Neither interface automatically restores a character;
the game must validate and apply its own resume-point schema.

Every outer request requires exactly command/session/value and a current Play
session ID. Its response contains `ok` and the parsed `result`. Pending close and
non-running Play allow only memory observations. The inner operation fields are exact:

| Command | Required fields besides command | Result |
| --- | --- | --- |
| snapshot | none | revision/data object |
| get | key | present/value object |
| set | key/value | revision object |
| remove | key | revision object |
| replace | data/revision | revision object |
| list-saves | none | list ticket |
| read-save | slot | read ticket |
| save | slot/overwrite boolean; optional snapshot | write ticket |
| load | slot/revision | load ticket |
| delete-save | slot | remove ticket |
| save-jobs | none | statuses without result payloads |
| save-status | id/operation | status without result payload |
| take-save | id/operation | observation or consumed terminal receipt |
| cancel-save | id/operation | null; only pending work can cancel |

A ticket is acceptance, not a successful disk result. After the mailbox response,
use returned IDs/kinds with `save-status` or `take-save`, for example:

```json
{"command":"game-data","session":"1","value":{"command":"take-save","id":"42","operation":"write"}}
```

The ID is illustrative. Keep polling pending/running receipts; only terminal
`succeeded` with `result` proves completion. A ready load still requires take and
may fail if live state changed. GUI controls use the same operations and poll their own tickets during frame
preparation even when the panel is hidden, leaving agent/script receipts for their
respective owners. Failures also reach the ordinary editor message.

File byte sizes also use decimal strings. Unknown/missing/duplicate fields, wrong
types and malformed revisions fail. The service bounds request bytes/depth; the
workspace/runtime transport imposes its own payload limit too. GUI slot listing is
cached and refreshed explicitly or after its own writes, avoiding per-frame disk
scans. Use `show-pane` with pane `game-data` to focus the actual docked panel.

## Validation and remaining work

Pure state/protocol regressions run without files or devices. Existing filesystem,
compiler and Play targets cover corruption/conflicts, read-only preparation, scene
replacement, source-free compiled restart and removal of isolated temporary saves.
FeatureLab audits the real SDK and supplies F6/F7 camera checkpoints across scenes.
Tests do not open desktop windows; actual editor checks use its product agent API.

Storage I/O runs in the owned worker during gameplay. Snapshot serialization,
validation/publication on the owner, shutdown draining, directory crash recovery,
authentication/cloud sync, broader game-specific schemas and finished gameplay remain
separate performance/capability boundaries. Games can display checkpoint feedback
through [native game text](GameUi.md) and use the
[native game clock](GameClock.md) during simulation pause. These contracts do not establish overall
engine production readiness.
