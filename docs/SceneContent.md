# Reusable scene content

Selected authoring subtrees can be saved as an ordinary `.dotscene` asset and
inserted into other scenes in the same project. Meshes, materials, clips, skins,
audio and scripts keep project-root paths; they are shared rather than copied into
a new package. The content file alone does not bundle those dependencies. This
supports independent authored copies and explicitly linked instances. Links use
saved source data; they do not automatically reload files during gameplay.

## Authoring and agent operations

Select one or more hierarchy entries. In Assets, enter a new `assets/...dotscene`
Path and Content name, then choose **Save selection as scene**. The last selected
entity determines the saved origin. Existing files, aliases and missing parents
fail; use the existing directory operation to prepare a parent. Saving content
leaves the active scene, scope, ordered selection, revision, dirty state, history
and entity allocator unchanged, including when the source has unsaved edits. It
publishes an external asset file, so document Undo does not remove that file.

```json
{"command":"save-selection-scene","revision":"12","path":"assets/MaterialStudies.dotscene","name":"Material studies content"}
```

The strict response owns the current document snapshot and `contentPath`. Use the
current revision, not the literal sample. Invalid/stale requests and external
project-metadata changes fail before publication. A response error after successful
file publication explicitly indicates that the operation may have committed.

Open the new asset with ordinary Open scene, edit, undo/redo and Save. Its content
need not contain a game camera; Scene view uses the editor camera. Select a
`.dotscene` asset and choose **Add scene instance**. It inserts at scene root with
identity placement and selects a new container; existing gizmos and hierarchy tools
then position or parent it. Agent callers can specify placement and parent in the
same transaction:

```json
{"command":"edit","revision":"13","operations":[{"op":"instantiate-scene","path":"assets/MaterialStudies.dotscene","parent":null,"transform":{"position":[2,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}}]}
```

Every field is required. Path may name an existing portable `.dotscene` under
assets or scenes. Parent is null or a live entity value in the current document;
transform is parent-local. Insertion creates a placement container plus every
content entity, returns all new values in `created`, and selects the container.
Native references map to these new values; existing entities and scene-wide
camera/environment/AO remain authoritative. Imported primary camera/listener flags
are demoted. One undo/redo entry restores content, transforms and selection together;
committed values are not reused on history branches. Malformed data, stale values,
invalid parents/transforms, missing/aliased sources, global scene limits and history
budgets reject the whole candidate without consuming published identity values.

Play, animation audition, active transform gestures, pending resource/entity drafts
and conflicting dialogs reject content saving. Ordinary authoring guards apply to
insertion. The GUI uses the same product operations; asynchronous mailbox
`queued:true` requires reading the applied response. A catalog refresh failure after
saving reports that the file was saved rather than claiming rollback.

## Native preparation

`Dot.World.SceneContent` is a CPU-only module with separate interface and
implementation. It performs no filesystem, window, rendering, audio or physics
initialization. The caller explicitly publishes the independently validated scene.

| Operation | Result and boundary |
| --- | --- |
| `extractSceneContent(source, selected, name)` | Descendant union, compact IDs and an identity container. Root world poses are flattened relative to the last selected entity's world origin; internal local transforms remain. Environment/AO are excluded; primary flags and opaque script data remain. |
| `prepareSceneInstance(target, content, firstValue, placement, parent)` | Fresh-scope candidate retaining target entity values, a placement root and owned source-to-target mapping. `firstValue` must respect the target's reserved allocation floor; authoring callers supply their history high-water mark. |

Both sources remain unchanged on success or failure. Returned handles address the
candidate's fresh scope; this is preparation, not in-place runtime instantiation.
Existing handles remain valid only in their original sources. Each new container
consumes one of the scene's 10000 slots and contributes to its depth limit of 256.
The final codec limit is 64 MiB. Creating-thread/moved-owner contracts remain the
same as Scene. No GPU or simulation service is implicitly staged.

Extraction accepts 1..10000 unique live handles. Ancestor/descendant overlap is
expanded once. Flattened roots require representable TRS; a sheared external-parent
basis fails rather than being approximated. Including that parent can retain the
local hierarchy. Skin joints and animation targets must remain within the extracted
union: select their common ancestor or all dependencies. These native references
remap on extraction and insertion. Arbitrary script JSON numbers/strings are not
interpreted as entity references, and resource paths remain project-root relative.
The mapping owns typed original/candidate handles and excludes the newly created
container, available separately as `root`.

Feature Lab's material content was validated by replacing the original studio group
with the saved asset at the original placement. Independent world-matrix comparison
and two distinct native viewport captures agree; this is specific workflow evidence,
not a claim of completed prefab or large-game authoring capability.


Saved content also preserves the scene's required `nextEntity` allocation metadata.
Deleting the highest source node, saving and reopening does not make that value
available for a replacement node. Scene extraction creates a new compact identity
namespace; insertion respects the target's existing reservations and writes the
advanced floor into the prepared candidate. This is necessary identity groundwork
for the linked updates described below.

## Linked instances and local overrides

Choose **Add linked scene** on a scene asset. Its placement root keeps an owned
source path, baseline and source-to-target value mapping. On that root, Inspector
provides **Open source scene**, **Refresh source**, **Restore source nodes** and
**Unlink scene**. Ordinary property edits create overrides by differing from the
last baseline. The equivalent strict document edits are:

```json
{"command":"edit","revision":"12","operations":[{"op":"link-scene","path":"assets/MaterialStudies.dotscene","parent":null,"transform":{"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}}]}
{"command":"edit","revision":"13","operations":[{"op":"refresh-scene-instance","entity":"20"}]}
{"command":"edit","revision":"14","operations":[{"op":"revert-scene-instance","entity":"20"}]}
{"command":"edit","revision":"15","operations":[{"op":"unlink-scene-instance","entity":"20"}]}
```

Each operation uses the existing document transaction/history, scope invalidation,
revision checks and selection rules. Root IDs in the examples are illustrative;
use `created` and the returned snapshot. Refresh/revert/unlink select the root.
Same-source no-op refresh preserves scope/revision/history. Batch failure publishes
neither allocation nor partial link/scene changes. Save/reopen retains baselines,
mappings and overrides; undo/redo restores them while entity allocation stays
monotonic. Save source edits before refreshing. Missing, aliased or malformed sources
fail without replacing the document. Linking the active file to itself is rejected.

Refresh performs a three-way merge of previous source, current local data and
incoming saved source. Unchanged properties follow source changes; local differences
win. Objects merge by property, arrays are atomic, and missing differs from null.
Native parent/skin/animation references are remapped before comparison. Script data
retains its literal values and is never interpreted as entity references. Existing
source nodes keep target values; new nodes allocate above the target reservation
floor. Locally deleted source nodes stay deleted on refresh. Source allocation must
not regress or reuse retired values. Imported primary flags remain demoted.

Removing a source node with local overrides is a conflict. Revert explicitly replaces
source-owned nodes with their current source values and restores local deletions;
placement roots, unrelated entities and local additions remain. Local children or
other references to removed nodes can still make the final scene invalid: repair
those relationships first. Invalid merged components and dangling references reject
the whole candidate. Unlink discards provenance while retaining materialized data.
Unlink placement roots before ungrouping. Duplicating or extracting linked subtrees
creates independent content, preserving the original link only on its original root.

Runtime/Play/export use materialized entities without reading link source files.
Source instances are flattened into their saved data when linked: recursive nested
refresh, variants, applying overrides back to source, source-path retargeting and
inline field markers remain unfinished. `Dot.World.SceneLinks`
provides CPU-only `prepareLinkedScene` and `prepareSceneLinkUpdate` candidates; Scene
owns validated link metadata. Existing target handles remain untouched until callers
publish the complete candidate.

## Inspecting and reverting applied overrides

Inspector's **Linked scene overrides** section works on instance roots, inherited
nodes and local additions. It shows changed properties, deleted inherited nodes
and added local nodes, with owned baseline/local values. **Whole instance** switches
between instance and selected-entity views; Previous/Next paginate the list. Revert
works per row or for the displayed page. Missing properties and JSON null remain
distinct, and arrays appear as one atomic change. Placement-root properties and
components stay local and are excluded from inherited overrides.

The same read-only query works through `DotEditor document` and the live mailbox:

```json
{"command":"inspect-scene-overrides","entity":"20","scope":"auto","offset":0,"limit":32}
```

Scope is `auto`, `instance` or `entity`. Auto chooses the whole instance on its
placement root and the selected entity otherwise. Direct link ownership takes
precedence over ancestor links, including inherited nodes reparented outside their
placement root. An unlinked live entity returns `linked:false` and an empty page.
Deleted nodes remain visible through their instance root. Limit is 1..256; offset
can equal total but cannot exceed it. Source-node/object-key traversal followed by
added-node value order gives stable pages. Queries require no revision and perform
no source-file I/O or document/history/selection changes.

The response contains document `revision`, `scope`, `scenePath`, `selected`, empty
`created` and `sceneOverrides`, without a full scene snapshot. The latter provides
`linked`, `root`, `asset`, `node`, effective `scope`, `offset`, `limit`, `total`,
`nextOffset` and `changes`. Each change has `kind` (`property`, `deleted-node`,
`added-node`), target `entity`, `path`, optional source value, and `baseline`/`local`
objects with `present` and typed JSON `value`. Property paths are escaped JSON
Pointers; node changes use an empty path. Copy only kind/entity/path into selectors.

```json
{"command":"edit","revision":"16","operations":[{"op":"revert-scene-overrides","root":"20","keys":[{"kind":"property","entity":"22","path":"/name"}]}]}
{"command":"edit","revision":"17","operations":[{"op":"revert-scene-baseline","root":"20"}]}
```

Selected revert accepts 1..256 unique current keys in one atomic edit. Unknown,
duplicate, invalid or stale selectors fail before publication. Dependent deleted
nodes can be restored together; source values keep their reserved target identities.
Added-node revert removes its subtree. Inherited descendants must be moved out,
either beforehand or through selected parent-property reverts in the same
transaction. Native references and final components still have to be valid.
Selection stays on surviving selected entities, falling back to the instance root.

**Revert all overrides** uses the stored baseline, restores inherited nodes and
removes local additions; placement roots and unrelated entities remain. It can
also repair local grouping around inherited nodes. **Restore source nodes** reads
the latest saved source and keeps local additions. **Refresh source** reads the
latest saved source while preserving local overrides. Stored-baseline reverts keep
working when the source asset is unavailable. Undo/redo restores all affected data,
and no-op full reverts preserve document revision/scope/history/allocation.

The live workspace exposes the current inspector page/query/revision/error under
`workspace.sceneOverrides`. A query for the currently selected entity also updates
that view, matching pagination/scope controls. Queries for other entities inspect
without moving selection. Applied overrides remain readable during pending drafts,
Play and preview; mutation controls use ordinary authoring guards. UI and agent
operations share the native `findSceneLinkRoot`, `inspectSceneOverrides`,
`prepareSceneOverrideRevert` and `prepareSceneBaselineRevert` implementations.
