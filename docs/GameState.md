# Gameplay state and save storage

`Dot.Game.State` and `Dot.Game.Saves` provide native storage foundations. They are
independent of scenes, devices and script realms. The application explicitly owns
their lifetime. This stage does not add built-in script bindings, editor operations,
automatic saving, project identity or per-user save-directory selection. A caller
can already share State through the existing native custom-service interface.

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

## Validation and remaining integration

Pure state regressions run in the unit target without filesystem or devices.
Existing filesystem and compiler targets cover slot persistence/failure and a real
GameSession with application-provided services across scene replacement and restart.
Those custom test bindings are not a shipped `dot:` API. Tests do not open windows.

The remaining product work includes stable application identity, per-user writable
directories, built-in script services with realm access restrictions, isolated Play
state and shared editor/agent operations. Synchronous save I/O can stall a frame;
background scheduling and crash-recovery policy also remain separate work.
