# Editor documents and automation

The editor currently provides native scene authoring through a shared document
service and JSON commands. The graphical editor is subsequent work. Runtime play
uses a separate GameSession scene; document editing opens no audio, physics,
compiler or window backend.

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

Components are camera, mesh, rigidBody, audioSource, audioListener and script.
Values use the same schema as native scene files; null removes the component.
Script authoring describes a source/class/properties/enabled attachment and does
not execute code. Asset existence belongs to runtime bindings, so documents may
author valid paths to resources that have not yet been created.

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

`--document-test <sdk-root> <existing-parent>` creates an owned temporary project,
selects and moves the attached cube, rejects its stale handle, undoes/redoes,
saves and reopens. `--document-render-test` additionally verifies changed actual
GPU pixels and exact restoration of the initial/edited frames on undo/redo.
These acceptances test the authoring model and renderer, without claiming a
graphical editor window. Local verification is on macOS; Windows/Linux common
code remains uncompiled/unexecuted locally.

The native [GUI rendering layer](Gui.md) adds persistent GPU previews, isolated
ImGui/SDL3/WebGPU contexts and an actual window/widget/document acceptance. This
enables the graphical editor frontend; it does not yet provide the complete
project editing window.
