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
initialization. GameSession itself only uses an explicitly supplied root and never
creates user directories implicitly.

Editor Play always owns a fresh temporary directory and fresh state. Stop explicitly
removes owned files even when Stop hooks fail. Cleanup failures report the path and
retain ownership for retry; restarting Play retries cleanup before creating another
store. Destruction/failed initial preparation perform best-effort RAII cleanup. A configured temporary parent still receives an owned
child, never direct writes. Passing a player save root to Play fails. Project and
installation files and real player progress remain separate. A retained game-data
handle permits in-memory snapshot/get after retirement; writes and all disk access
fail after close, fault or destruction.

## Script API

```ts
import {gameState, saves} from 'dot:engine';

gameState.set('checkpoint', {level: 'scenes/main.dotscene', score: 7});
saves.write('checkpoint', false); // Existing slots require explicit true to replace.
const stored = saves.read('checkpoint'); // Owned snapshot; live state is unchanged.
saves.load('checkpoint', gameState.snapshot().revision);
```

`gameState.snapshot/get/set/remove/replace` operate on owned finite JSON values.
Missing `get` returns undefined; an actual null value remains null. Revisions are
canonical decimal strings. `replace(data, revision)` and `saves.load(slot, revision)`
reject stale current-state revisions; a saved revision is not an assignment token.
`saves.list/read/write/load/remove` are explicit filesystem operations. Loading
replaces game data, not the scene or entity transforms: game code applies its own
checkpoint schema and requests a scene if appropriate.

Only in-memory snapshot/get are read-only preparation services. Constructors and
migration cannot mutate state or access save files, including disk reads. Once the
scene starts, Stop hooks can save before normal retirement. A failed candidate
constructor cannot publish into the active game's shared state. Script non-JSON
values are rejected by the existing runtime transport before service dispatch.

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

Every outer request requires exactly command/session/value and a current Play
session ID. Its response contains `ok` and the parsed `result`. Pending close and
non-running Play allow only snapshot/get. The inner operation fields are exact:

| Command | Required fields besides command | Result |
| --- | --- | --- |
| snapshot | none | revision/data object |
| get | key | present/value object |
| set | key/value | revision object |
| remove | key | revision object |
| replace | data/revision | revision object |
| list-saves | none | name/bytes array |
| read-save | slot | saved revision/data object |
| save | slot/overwrite boolean | name/bytes object |
| load | slot/revision | current revision object |
| delete-save | slot | null |

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

Save I/O is synchronous and can stall a frame. Background scheduling, directory
crash recovery, authentication/cloud sync, game-specific checkpoint schemas and
finished gameplay remain separate work. These contracts do not establish overall
engine production readiness.
