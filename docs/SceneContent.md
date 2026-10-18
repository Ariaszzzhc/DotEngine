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
Editor insertion/refresh resolves saved linked sources recursively before flattening
the resulting materialization. Whole-field application is described below; variants,
whole-instance/topology application remain unfinished. Explicit source-path relinking
is described below.
`Dot.World.SceneLinks` provides CPU-only `prepareLinkedScene` and `prepareSceneLinkUpdate` candidates; Scene
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
`added-node`), target `entity`, `path`, canonical-baseline `source` reference and
persistent `sourceKey` array (both null for local added nodes), and `baseline`/`local`
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

## Field indicators and restoring a component

The entity Inspector marks overridden Transform/component headings with `*` and
shows applied counts beside the corresponding fields, including Name, Parent and
removed components. **Restore field** restores that entire top-level field from
the last refreshed baseline in one document transaction. Other fields, placement,
selection and entity reservations remain. Undo/redo and Save use ordinary history.
Pending Inspector/resource drafts must be applied or reset first; indicators count
only committed authoring differences, including during Play or audition.

`inspect-scene-overrides.sceneOverrides.fields` maps top-level field names to
complete property-override counts for the requested entity/instance filter. Counts
are independent of offset/limit, including an empty final page. Added/deleted-node
entries contribute to `total`, but have no inherited field counts. An unlinked
entity and an unchanged placement root return empty fields. The workspace exposes
the selected entity's independent filter under `workspace.sceneFieldOverrides`,
with revision, page and error; changing the lower list to Whole instance does not
change field indicators.

```json
{"command":"edit","revision":"17","operations":[{"op":"revert-scene-field","root":"20","entity":"22","field":"material"}]}
```

Every field is required, with no extras. `field` is one native top-level entity
field, such as `name`, `parent`, `transform`, `mesh`, `material` or `script`. It is
not a JSON Pointer. `id`, unknown fields, stale requests, missing nodes, placement
roots, local additions and nodes outside that instance reject. Use existing node
revert operations for additions/deletions. Unchanged fields are no-ops.

`Dot.World.SceneLinks::prepareSceneFieldRevert` uses the stored, target-remapped
baseline without source I/O or new entity IDs. It restores complete components,
including all script properties even beyond the 256-key selected-revert limit.
Arrays remain atomic and absent components remain null. Invalid final parent,
skin, animation or mutually exclusive component relationships fail atomically;
repair dependencies or restore related fields in one edit batch. It does not write
shared material/clip assets or apply local overrides back to the source.

Prism Relay's twelve straight ducts and fourteen supports are linked to
`assets/airducts/straight.dotscene` and `brace.dotscene`. Open and save a source,
return to the bay, then explicitly refresh the desired placement roots. Play and
exports keep using saved materializations. Field indicators and Restore field
share this same native operation with strict agent editing.

## Layered source identities

A source node's logical key is separate from the integer used to materialize an
intermediate scene. Local nodes use `[sourceValue]`; nested nodes prefix that key
with the source instance-root value. An assembly can therefore retain `[root,leaf]`
while its temporary entity integer changes, or another local node uses the old
temporary integer. Refresh matches logical keys, preserving target values, native
references and local overrides for continuing nodes. Different keys receive fresh
target allocations. Removal of an overridden source node remains a conflict.

Namespace observations retain original asset paths, source allocator floors and
live physical source allocations. Existing namespace assets/floors cannot change/
regress; retired values cannot be introduced as new visible origins. Hidden inherited
nodes remain known in their underlying source namespace and can later reappear with
fresh target bindings after removal. Source unlinking promotes previously saved
inherited physical allocations to local identities, retiring the former nested
bindings. Scope-local handles and arbitrary script property literals are unaffected.

`Dot.World.SceneSources` captures an independent saved/native source with
`captureSceneSource`. `prepareSceneSourceSnapshot` consumes an explicit working scene,
original local origins and original physical namespace, so temporary allocations
cannot become source identity. `SceneSourceSnapshot` includes materialized entities,
asset path, origins and namespace observations and is validated at preparation
boundaries. Typed SceneLinks overloads accept these snapshots; raw Scene overloads
capture ordinary saved/native inputs. All factories are CPU-only and perform no I/O.

Insertion/refresh baselines use compact IDs in logical-key order and demoted primary flags.
Their allocator belongs to that canonical representation; original source allocation
is retained in namespaces. Equivalent working-ID reorderings produce identical
baseline/target serialization and do not cause artificial document edits. Source
keys remain observable in override queries even when canonical baseline references
change. Strict scene assets were replaced together with this metadata change;
missing provenance is rejected without a compatibility loader.

## Recursive source resolution

`Dot.World.SceneResolver` resolves saved source dependencies before editor
`link-scene`, `instantiate-scene`, `refresh-scene-instance`, `revert-scene-instance`
and `retarget-scene-instance`.
Every intermediate source refreshes its own links before its parent is merged.
Intermediate property/deletion/addition overrides remain in effect, even when the
outer operation restores source nodes. Removing a node overridden in an intermediate
source is a conflict; an outer restore never erases another source's authoring edits.
Independent insertion resolves the same graph, then discards imported link metadata.

Sources are read once per distinct portable asset path within one operation and
shared dependencies are materialized independently at each placement. Instances
process in root-value order, making temporary allocations deterministic. Original
local identities and physical reservations are captured before processing links;
working allocations never become source-local origins. Each resolution has a fresh
cache. Consecutive document source updates reuse only their current resolved source
within that edit segment; later edits observe newly saved files without persistent
caches. Caller serialization of source writers still applies.

Cycles and any reference back to the active linked document reject. Missing,
malformed, aliased/symlink or inaccessible dependencies also reject; embedded
baselines are not used as silent fallbacks. Errors carry the traversed source chain.
Resolution performs no source writes and publishes no intermediate document/history/
selection/allocation changes. It reads saved assets, without applying unsaved source
documents or providing a filesystem-wide snapshot against concurrent external edits.
Stored-baseline revert, unlink, loading, Play and export retain their source-independent
behavior. Existing saved materializations are not implicitly refreshed on open.

Defaults bound one graph to 32 source levels, 256 distinct sources, 4096 saved links,
64 MiB aggregate input text, 128 MiB aggregate resolved scene serialization and 100000
entity visits. Entity visits count each decoded scene and every refreshed candidate,
including repeated placements; encoded storage counts cached unique resolved scenes.
Native per-scene entity/provenance/serialization limits still apply. Shared cached
subtrees must fit at every placement depth, including deeper cache hits. Native
callers may lower these limits; higher/zero limits reject before reading. The loader
receives the remaining input quota and returned text is checked before decoding.
Resolution is synchronous; background authoring I/O and incremental dependency
tracking remain future work.

## Applying a complete field to its source

An overridden live inherited node now has **Apply field to source** beside **Restore
field**. It writes that complete native field to the saved immediate source and
rebases this instance, retaining placement and other local overrides. Other instances
refresh explicitly. This edits a shared asset: scene Undo restores the document's
previous baseline/local edits, but does not undo the source file. Save the active
scene explicitly after reviewing the result.

```json
{"command":"apply-scene-field","revision":"18","root":"20","entity":"22","field":"material"}
```

This is a separate document/workspace command, not an `edit` batch operation. All
fields are required, extras reject, and the current revision is mandatory. The
response adds `sourceApplied` with `path`, `written` and `sceneChanged`. A no-op
preserves document revision/scope/history/allocation and does not write the file.
If someone already saved exactly the local value, the source need not be rewritten;
the instance still adopts its current baseline. Applying an unchanged local field
is a no-op rather than an unrelated source refresh.

The native `prepareSceneFieldApply` is CPU-only and returns a prepared source scene
and instance update. Comparisons/remapping use persistent logical source keys,
including nested inherited nodes, rather than target or temporary materialization
IDs. Native parent/skin/animation references must belong to that source; arbitrary
script JSON remains literal. A detached parent outside the placement cannot be
applied. Placement-local primary camera/listener flags must be edited directly in
the source; ordinary field changes preserve the source's primary setting while
inherited target flags remain demoted. Additions/deletions/placement roots and `id`
are not field operations. Whole-instance contents (including topology) use the separate operation below;
arbitrary selectively applied override pages remain unfinished.

If the selected source field changed since the instance baseline to a different
value, Apply fails as a conflict. Refresh first, inspect the new baseline/local
values, then deliberately apply the desired field. Final source and target component,
hierarchy and reference validation must succeed. Unrelated source fields are retained;
other local overrides follow ordinary three-way refresh rules.

The operation resolves current saved dependencies before preparation. A changed
source is saved as that refreshed immediate materialization, including its nested
link metadata/updates; nested source files are not written. No-write rebases keep
the original source namespace observations and do not promote temporary IDs to
saved reservations. Changed field and whole-instance preparations both retain the
latest source's physical reservation floor, including permanent exhaustion. File text/project metadata/dependency bytes are checked again
before publication. Caller serialization is still required; this is not a filesystem
transaction against hostile races.

The document prepares bounded history and allocation before saving. A source-write
failure retains the document and explicitly reports that the file may have committed;
a response/catalog failure after publication likewise reports possible/actual effects.
Pending drafts, Play, animation audition, transform gestures and conflicting dialogs
reject workspace application. GUI and agent commands share the same operation.


## Applying complete instance contents

Inspector's **Apply instance contents to source** applies the entire linked body's
properties, additions, deletions and hierarchy to its saved immediate source. The
placement container's name/parent/transform and active scene settings remain local.
Other placements and documents refresh explicitly. The strict shared operation is:

```json
{"command":"apply-scene-instance","revision":"18","root":"20"}
```

This is a source publication command, not an `edit` batch. It shares field apply's
revision/draft/Play/dialog guards, exact source/dependency observation checks,
prepared document/history publication and `sourceApplied` report. A scene Undo
restores the local scene/baseline, not the shared source file. Save after review.
No applied override means no write or unrelated refresh. If the saved source
already contains every local change, rebase without rewriting it; original source
namespace observations are retained.

Independent source object leaves survive; competing leaf values, deleting a node
that changed in the source, or editing a node removed by the source reject. Arrays
remain atomic values. Deletion dependencies, parent cycles, references, primary
settings, scene/metadata capacities and final source/target decoding must all pass
before publication. Native parent/skin/animation references remap by logical identity;
references outside the body and placement-local primary flags reject. Script JSON
remains literal. Applying an instance is not a way to transfer its primary camera
or listener to every placement.

New nodes receive fresh source values beyond the latest source's reserved floor,
while keeping their existing target entity values. Source-side independent additions
allocate only the needed new target values. Deleted values remain reserved. Local
linked additions become nested links in the source, including remapped missing-node
reservations; the chosen instance then inherits their flattened provenance through
its outer link. Foreign link ownership cannot be captured by moving one of its
children into the body. Existing nested source node edits remain overrides in the
immediate saved source; nested source files are not written.

The proposed saved dependency graph is resolved before writing, including newly
introduced nested links. Direct/indirect cycles and active-document dependencies
reject without effects. Fresh nested updates are incorporated and the chosen
instance is rebased against the exact materialization being saved. Source graph
budgets and caller serialization still apply; byte guards plus atomic replacement
are not an OS-wide transaction against arbitrary writers. A file-write error may
mean the file committed while the document was retained, and is reported as such.

Native `prepareSceneInstanceApply` is CPU-only and returns `SceneSourceApply`, shared
with field preparation. It mutates neither input, preserves placement/unrelated
entities/globals, and prepares both sides before publication. Native callers must
resolve the proposed dependency graph before publishing a changed source and must
honor `sourceChanged` for no-write rebases. General variants, selective topology
application and automatic project-wide refresh/relocation remain unfinished.

## Relinking moved or copied sources

Select the instance or one of its inherited nodes, then choose the replacement
`.dotscene` in Assets. Inspector's **Use selected scene as source** relinks that
instance. **Relink all N instances in scene** relinks every placement of its current
immediate source in the active document as one undoable edit. Other documents need
their own explicit edits. Source files are not moved or written by relinking.

```json
{"command":"edit","revision":"18","operations":[{"op":"retarget-scene-instance","entity":"20","path":"assets/relocated/Equipment.dotscene"}]}
```

Use the link root's value in `entity`. The operation requires exactly op/entity/path;
the enclosing edit carries the current revision. To relink several placements,
put one operation per root in the same operations array, as the Inspector does.
The ordinary 1..256 operation/1 MiB edit limits apply; the all-in-scene button is
disabled above 256 placements, and larger sets need explicit smaller batches.
The entire batch commits once or fails without changing selection/history/allocation
or publishing a partial set of paths. The last operation's root becomes selected.

Choosing a replacement explicitly asserts that it is a moved/copied version of the
same source lineage. Correspondence uses persistent logical node keys; names and
hierarchy appearance do not establish lineage. There is no independent file UUID
proof for arbitrary unrelated scenes that happen to reuse the same numeric keys.
Use the saved copy's preserved identities and review the resulting overrides.

The new saved source is resolved with the normal cycle, path, case, dependency and
aggregate graph budgets. The old file is never read, so a missing old path can be
repaired. The active document cannot become its own source, directly or through
dependencies. Only the root namespace asset path changes; nested namespaces keep
their source assets and all allocation floors/retired-value reservations retain
ordinary refresh validation. New keys allocate above the document's reserved floor.

The same three-way merge preserves local property overrides, local deletions and
additions, placement transforms, stable target values and native references. Other
instances stay unchanged unless included in the batch. Removed overridden nodes,
dangling references and invalid merged components reject. Imported primary camera/
listener flags remain demoted. Parent/skin/animation references use the ordinary
validated remapping; script JSON values remain literal. This is an explicit
source association followed by refresh, without implicit matching by node names.

Undo/redo, Save/reopen, override inspection/baseline restore, future Refresh source
and Apply field to source all use the new association. Undo restores the prior
association/baseline even if that file remains missing; it does not undo external
file moves or edits. Choosing the current path performs ordinary refresh; if the
source and selection are already current, it leaves revision/history unchanged.
Play and exports keep using materialized content without reading authoring sources.

Native `prepareSceneLinkRetarget` accepts a validated `SceneSourceSnapshot` and
returns a CPU-only independent `SceneLinkUpdate`. It shares the ordinary refresh
implementation and publishes no input scene or source file. Scene/document limits
and failure/ownership contracts remain unchanged; this does not provide automatic
project-wide asset relocation or arbitrary model/skeleton correspondence authoring.


## Preparing multiple source updates

Inspector's **Refresh all N instances in scene** submits the same strict edit array
available to agents, applying the current saved source to its placements together.
The ordinary 256-operation/1 MiB limits and draft/Play guards apply.

Consecutive refresh/restore-source/relink operations in one edit array now share
one owned native candidate. Their order, local overrides, target values, namespace
reservations and selection behavior remain explicit. A different edit kind ends
the segment; the following source segment starts from that staged document. The
whole edit still publishes one scene/history step or nothing. Repeated roots follow
the preceding staged path/baseline, including retarget followed by refresh.

The editor retains only the current resolved source in each segment. Adjacent
requests for the same path reuse that snapshot; changing path drops it before
reading the next graph. Nothing survives into another edit, so failed/retried edits
reread saved files. This retains existing caller-serialized file semantics and
per-graph budgets, without promising a filesystem-wide snapshot. Source files are
never written by these edits.

`Dot.World.SceneLinks::SceneLinkBatch` is the shared CPU-only native foundation.
Begin captures target data once; append accepts 1..256 ordered refresh, restore-
source or retarget actions; finish consumes the transaction and returns one
independent validated scene. Inputs can be destroyed after their calls because
the transaction retains owned JSON/provenance rather than borrowed Scene handles.
Move preserves the creator thread. An owner-thread append failure faults the entire
transaction; finish cannot publish its earlier work. Finish is single-use.

Each append enforces the existing working-node, baseline, mapping, namespace and
observed physical-reservation budgets. Replaced metadata vectors release their old
capacity; surviving created values are reported while consumed allocation values
remain reserved. Final component/reference/hierarchy validation happens together.
A temporary dangling reference may be repaired by a later explicit restore in that
same source segment; leaving it unrepaired rejects the final scene. Normal source
removal/override conflicts still reject during append. Native single-source helpers
use this same implementation.

This removes repeated full-target reconstruction inside a source update segment.
Mixed synchronous edits retain owner-side resolution/publication. Dedicated source
jobs move resolution/preparation to an owned worker; resource streaming and
large-scene frame deadlines remain separate unfinished work.

Source refresh/restore/retarget can also be prepared as one owned editor background
job. It captures authored bytes and stable document identity without sharing a live
Scene. Recursive file observations are bounded and rechecked on the worker when
Apply is requested; final owner publication requires the exact captured revision
and scene path. Changed results transfer sealed CPU ownership with canonical text,
so the owner does not repeat scene decoding/encoding. The same worker retires the
old CPU descriptions; no-op results preserve the existing scene/history/snapshot. See [background update controls](Editor.md#background-scene-source-updates).
